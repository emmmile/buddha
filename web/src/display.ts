// Display pipeline on the GPU (display.wgsl): channel maxima, tone curve and per-pixel
// adjustments, blurred luminance for clarity and texture, then the canvas.

// smooth_blur passes of buddha_image::adjust: radius and pass count.
const CLARITY_BLUR = { radius: 4, passes: 3 };
const TEXTURE_BLUR = { radius: 2, passes: 2 };

// Exposure and gamma set the tone curve; the adjustments use the native ranges, -100..100.
export interface DisplaySettings {
  exposure: number;
  gamma: number;
  brightness: number;
  contrast: number;
  saturation: number;
  clarity: number;
  texture: number;
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
  private uniform: GPUBuffer;
  private maxima: GPUBuffer;
  private width = 0;
  private height = 0;
  private reduceGroup?: GPUBindGroup;
  private toneGroup?: GPUBindGroup;
  private shadeGroup?: GPUBindGroup;
  private clarityGroups: GPUBindGroup[] = [];
  private textureGroups: GPUBindGroup[] = [];

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
    this.shadePipeline = device.createRenderPipeline({
      layout: "auto",
      vertex: { module, entryPoint: "fullscreen" },
      fragment: { module, entryPoint: "shade", targets: [{ format }] },
    });
    this.uniform = device.createBuffer({
      size: 48,
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
    const broad = storage(pixels * 4);
    const fine = storage(pixels * 4);

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
      11: broad,
      12: fine,
    });

    // Each blur pass runs rows into scratch, then columns into the target; later passes start
    // from the target.
    const blur = ({ radius, passes }: typeof CLARITY_BLUR, target: GPUBuffer) => {
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
    this.clarityGroups = blur(CLARITY_BLUR, broad);
    this.textureGroups = blur(TEXTURE_BLUR, fine);
  }

  draw(s: DisplaySettings) {
    if (!this.reduceGroup || !this.toneGroup || !this.shadeGroup) return;
    const data = new ArrayBuffer(48);
    new Uint32Array(data, 0, 2).set([this.width, this.height]);
    new Float32Array(data, 8, 7).set([
      s.gamma,
      s.exposure,
      Math.pow(2, s.brightness / 50),
      1 + s.contrast / 100,
      1 + s.saturation / 100,
      s.clarity / 100,
      s.texture / 100,
    ]);
    this.device.queue.writeBuffer(this.uniform, 0, data);

    const groups = Math.ceil((this.width * this.height) / 256);
    const encoder = this.device.createCommandEncoder();
    encoder.clearBuffer(this.maxima);
    const pass = encoder.beginComputePass();
    pass.setPipeline(this.reducePipeline);
    pass.setBindGroup(0, this.reduceGroup);
    pass.dispatchWorkgroups(groups);
    pass.setPipeline(this.tonePipeline);
    pass.setBindGroup(0, this.toneGroup);
    pass.dispatchWorkgroups(groups);
    pass.setPipeline(this.blurPipeline);
    const blurs = [
      ...(s.clarity ? this.clarityGroups : []),
      ...(s.texture ? this.textureGroups : []),
    ];
    for (const group of blurs) {
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(groups);
    }
    pass.end();

    const render = encoder.beginRenderPass({
      colorAttachments: [
        {
          view: this.target.getCurrentTexture().createView(),
          loadOp: "clear",
          storeOp: "store",
          clearValue: [0, 0, 0, 1],
        },
      ],
    });
    render.setPipeline(this.shadePipeline);
    render.setBindGroup(0, this.shadeGroup);
    render.draw(3);
    render.end();
    this.device.queue.submit([encoder.finish()]);
  }
}
