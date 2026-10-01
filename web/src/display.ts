// Display pipeline on the GPU (display.wgsl): channel maxima, tone curve and saturation, blurred
// luminance for texture and sharpness, then the canvas.

// Texture uses two 5x5 box passes; sharpness uses one 3x3 box.
const TEXTURE_BLUR = { radius: 2, passes: 2 };
const SHARPNESS_BLUR = { radius: 1, passes: 1 };

// Pixel layout of captured images, as ImageData expects.
const CAPTURE_FORMAT: GPUTextureFormat = "rgba8unorm";

// Exposure and gamma are the tone curve of buddha++; the adjustments range over -100..100.
export interface DisplaySettings {
  exposure: number; // stops: the tone curve is scaled by 2^exposure
  gamma: number;
  saturation: number;
  texture: number;
  sharpness: number;
}

interface Target {
  getCurrentTexture(): GPUTexture;
}

export class Display {
  private device: GPUDevice;
  private target: Target;
  private reducePipeline: GPUComputePipeline;
  private tonePipeline: GPUComputePipeline;
  private blurPipeline: GPUComputePipeline;
  private shadePipeline: GPURenderPipeline;
  private capturePipeline: GPURenderPipeline;
  private uniform: GPUBuffer;
  private maxima: GPUBuffer;
  private width = 0;
  private height = 0;
  private reduceGroup?: GPUBindGroup;
  private toneGroup?: GPUBindGroup;
  private shadeGroup?: GPUBindGroup;
  private captureGroup?: GPUBindGroup;
  private textureGroups: GPUBindGroup[] = [];
  private sharpnessGroups: GPUBindGroup[] = [];

  constructor(
    device: GPUDevice,
    module: GPUShaderModule,
    target: Target,
    format: GPUTextureFormat
  ) {
    this.device = device;
    this.target = target;
    const compute = (entryPoint: string) =>
      device.createComputePipeline({ layout: "auto", compute: { module, entryPoint } });
    this.reducePipeline = compute("reduce");
    this.tonePipeline = compute("tone");
    this.blurPipeline = compute("blur");
    const shade = (format: GPUTextureFormat) =>
      device.createRenderPipeline({
        layout: "auto",
        vertex: { module, entryPoint: "fullscreen" },
        fragment: { module, entryPoint: "shade", targets: [{ format }] },
      });
    this.shadePipeline = shade(format);
    this.capturePipeline = shade(CAPTURE_FORMAT);
    this.uniform = device.createBuffer({
      size: 32,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.maxima = device.createBuffer({
      size: 16,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
  }

  private group(
    pipeline: GPUComputePipeline | GPURenderPipeline,
    buffers: Record<number, GPUBuffer>
  ) {
    return this.device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: Object.entries(buffers).map(([binding, buffer]) => ({
        binding: Number(binding),
        resource: { buffer },
      })),
    });
  }

  setHistogram(histogram: GPUBuffer, width: number, height: number) {
    this.width = width;
    this.height = height;
    const pixels = width * height;
    const storage = (size: number) =>
      this.device.createBuffer({ size, usage: GPUBufferUsage.STORAGE });
    const color = storage(pixels * 16);
    const luma = storage(pixels * 4);
    const scratch = storage(pixels * 4);
    const textureBlur = storage(pixels * 4);
    const sharpnessBlur = storage(pixels * 4);

    this.reduceGroup = this.group(this.reducePipeline, {
      0: this.uniform,
      1: histogram,
      2: this.maxima,
    });
    this.toneGroup = this.group(this.tonePipeline, {
      0: this.uniform,
      1: histogram,
      3: this.maxima,
      4: color,
      5: luma,
    });
    this.shadeGroup = this.group(this.shadePipeline, {
      0: this.uniform,
      9: color,
      10: luma,
      11: textureBlur,
      12: sharpnessBlur,
    });
    this.captureGroup = this.group(this.capturePipeline, {
      0: this.uniform,
      9: color,
      10: luma,
      11: textureBlur,
      12: sharpnessBlur,
    });

    // Each blur pass runs rows into scratch, then columns into the target; later passes start
    // from the target.
    const blur = ({ radius, passes }: typeof TEXTURE_BLUR, target: GPUBuffer) => {
      const axis = (vertical: number) => {
        const uniform = this.device.createBuffer({
          size: 16,
          usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        this.device.queue.writeBuffer(
          uniform,
          0,
          new Uint32Array([width, height, radius, vertical])
        );
        return uniform;
      };
      const rows = axis(0);
      const columns = axis(1);
      const groups: GPUBindGroup[] = [];
      for (let pass = 0; pass < passes; ++pass) {
        groups.push(
          this.group(this.blurPipeline, { 6: rows, 7: pass ? target : luma, 8: scratch })
        );
        groups.push(this.group(this.blurPipeline, { 6: columns, 7: scratch, 8: target }));
      }
      return groups;
    };
    this.textureGroups = blur(TEXTURE_BLUR, textureBlur);
    this.sharpnessGroups = blur(SHARPNESS_BLUR, sharpnessBlur);
  }

  // Encodes the whole pipeline, rendering into view with the given shade pipeline and group.
  private encode(
    s: DisplaySettings,
    view: GPUTextureView,
    shadePipeline: GPURenderPipeline,
    shadeGroup: GPUBindGroup
  ) {
    const data = new ArrayBuffer(32);
    new Uint32Array(data, 0, 2).set([this.width, this.height]);
    new Float32Array(data, 8, 5).set([
      s.gamma,
      Math.pow(2, s.exposure),
      1 + s.saturation / 100,
      s.texture / 100,
      s.sharpness / 100,
    ]);
    this.device.queue.writeBuffer(this.uniform, 0, data);

    const groups = Math.ceil((this.width * this.height) / 256);
    const encoder = this.device.createCommandEncoder();
    encoder.clearBuffer(this.maxima);
    const pass = encoder.beginComputePass();
    pass.setPipeline(this.reducePipeline);
    pass.setBindGroup(0, this.reduceGroup!);
    pass.dispatchWorkgroups(groups);
    pass.setPipeline(this.tonePipeline);
    pass.setBindGroup(0, this.toneGroup!);
    pass.dispatchWorkgroups(groups);
    pass.setPipeline(this.blurPipeline);
    const blurs = [
      ...(s.texture ? this.textureGroups : []),
      ...(s.sharpness ? this.sharpnessGroups : []),
    ];
    for (const group of blurs) {
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(groups);
    }
    pass.end();

    const render = encoder.beginRenderPass({
      colorAttachments: [{ view, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 1] }],
    });
    render.setPipeline(shadePipeline);
    render.setBindGroup(0, shadeGroup);
    render.draw(3);
    render.end();
    return encoder;
  }

  draw(s: DisplaySettings) {
    if (!this.shadeGroup) return;
    const view = this.target.getCurrentTexture().createView();
    this.device.queue.submit([this.encode(s, view, this.shadePipeline, this.shadeGroup).finish()]);
  }

  // The image draw shows, read back from the GPU at display resolution.
  async capture(s: DisplaySettings): Promise<ImageData> {
    if (!this.captureGroup) throw new Error("Nothing to capture yet");
    const { width, height } = this;
    const texture = this.device.createTexture({
      size: [width, height],
      format: CAPTURE_FORMAT,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    const bytesPerRow = Math.ceil((width * 4) / 256) * 256; // copies need 256-byte rows
    const buffer = this.device.createBuffer({
      size: bytesPerRow * height,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });
    const encoder = this.encode(s, texture.createView(), this.capturePipeline, this.captureGroup);
    encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow }, [width, height]);
    this.device.queue.submit([encoder.finish()]);
    try {
      await buffer.mapAsync(GPUMapMode.READ);
      const rows = new Uint8Array(buffer.getMappedRange());
      const pixels = new Uint8ClampedArray(width * height * 4);
      for (let y = 0; y < height; ++y) {
        pixels.set(rows.subarray(y * bytesPerRow, y * bytesPerRow + width * 4), y * width * 4);
      }
      return new ImageData(pixels, width, height);
    } finally {
      buffer.destroy();
      texture.destroy();
    }
  }
}
