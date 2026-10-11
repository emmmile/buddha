// End-to-end preview submission/completion timings on real WebGPU hardware (no browser).
// Sampling is excluded; the default tone/texture/sharpness passes are included.
import { Display } from "../src/display.ts";
const { Deno, navigator, GPUTextureUsage, GPUBufferUsage, performance, URL } = globalThis;
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) throw new Error("No GPU");
const source = await Deno.readTextFile(new URL("../src/shaders/display.wgsl", import.meta.url));
const settings = { exposure: 0.2, gamma: 0.3, saturation: 100, texture: 25, sharpness: 75 };
const transform = { scale: [1.3, 1.3], offset: [-0.15, -0.15], opacity: 0.5 };
for (const [width, height] of [
  [1920, 1080],
  [3840, 2160],
]) {
  const device = await adapter.requestDevice({
    requiredLimits: {
      maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
      maxBufferSize: adapter.limits.maxBufferSize,
    },
  });
  device.pushErrorScope("validation");
  const module = device.createShaderModule({ code: source });
  const target = device.createTexture({
    size: [width, height],
    format: "bgra8unorm",
    usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT,
  });
  const display = new Display(device, module, { getCurrentTexture: () => target }, "bgra8unorm");
  const histogram = device.createBuffer({
    size: width * height * 12,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  display.setHistogram(histogram, width, height);
  device.queue.writeBuffer(histogram, 0, new Uint32Array(width * height * 3).fill(10));
  display.draw(settings);
  display.preserve();
  for (const mode of ["steady", "fade", "repeated-zoom"]) {
    const draw = () => {
      if (mode === "repeated-zoom") display.preserve();
      display.draw(settings, mode === "steady" ? undefined : transform);
    };
    for (let i = 0; i < 8; i++) draw();
    await device.queue.onSubmittedWorkDone();
    const samples = [];
    for (let round = 0; round < 5; round++) {
      const start = performance.now();
      for (let i = 0; i < 12; i++) draw();
      await device.queue.onSubmittedWorkDone();
      samples.push((performance.now() - start) / 12);
    }
    samples.sort((a, b) => a - b);
    globalThis.console.log(
      JSON.stringify({ size: `${width}x${height}`, mode, median_ms: Number(samples[2].toFixed(3)) })
    );
  }
  const error = await device.popErrorScope();
  if (error) throw new Error(error.message);
  target.destroy();
  device.destroy();
}
