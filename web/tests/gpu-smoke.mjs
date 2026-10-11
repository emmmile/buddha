// Optional headless check on real WebGPU hardware. Run with yarn test:gpu (requires Deno).
import { Display } from "../src/display.ts";
const { Deno, navigator, GPUBufferUsage, GPUTextureUsage, GPUMapMode, URL } = globalThis;

// Deno has WebGPU but no DOM ImageData. Capture needs only this data container.
globalThis.ImageData = class {
  constructor(data, width, height) {
    this.data = data;
    this.width = width;
    this.height = height;
  }
};
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) throw new Error("No WebGPU adapter for the headless smoke check");
const device = await adapter.requestDevice();
device.pushErrorScope("validation");
const source = await Deno.readTextFile(new URL("../src/shaders/display.wgsl", import.meta.url));
const module = device.createShaderModule({ code: source });
const errors = (await module.getCompilationInfo()).messages.filter((m) => m.type === "error");
if (errors.length) throw new Error(JSON.stringify(errors));
const size = 4;
const target = device.createTexture({
  size: [size, size],
  format: "bgra8unorm",
  usage: GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT,
});
const display = new Display(device, module, { getCurrentTexture: () => target }, "bgra8unorm");
const histogram = device.createBuffer({
  size: size * size * 12,
  usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
});
display.setHistogram(histogram, size, size);
const settings = { exposure: 0, gamma: 0.3, saturation: 0, texture: 0, sharpness: 0 };
const read = async () => {
  const buffer = device.createBuffer({
    size: 256 * size,
    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture: target }, { buffer, bytesPerRow: 256 }, [size, size]);
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(GPUMapMode.READ);
  const data = new Uint8Array(buffer.getMappedRange()).slice();
  buffer.destroy();
  return data;
};
const check = (condition, message) => {
  if (!condition) throw new Error(message);
};
check(!display.preserve(), "A fresh histogram cannot preserve an undrawn frame");
device.queue.writeBuffer(histogram, 0, new Uint32Array(size * size * 3).fill(10));
display.draw(settings);
check((await read())[0] === 255, "Initial frame should be white");
check(display.preserve(), "Previous preview should be available after a draw");
const clear = device.createCommandEncoder();
clear.clearBuffer(histogram);
device.queue.submit([clear.finish()]);
const transform = { scale: [0.5, 0.5], offset: [0.25, 0.25], opacity: 1 };
display.draw(settings, transform);
check((await read())[0] === 255, "Previous image should cover an empty new histogram");
const clean = await display.capture(settings);
check(
  clean.data[0] === 0 && clean.data[3] === 255,
  "Export must exclude the placeholder and remain opaque"
);
display.draw(settings, { ...transform, opacity: 0.5 });
const halfway = (await read())[0];
check(halfway >= 127 && halfway <= 128, "Half-faded old image should blend over black");
check(display.preserve(), "Rapid navigation should preserve the blended on-screen frame");
display.draw(settings, { scale: [1, 1], offset: [0, 0], opacity: 1 });
check((await read())[0] === halfway, "Repeated zoom must retain the blended previous frame");
display.draw(settings, { scale: [2, 2], offset: [-0.5, -0.5], opacity: 1 });
const zoomout = await read();
check(
  zoomout[0] === 0 && zoomout[256 + 4] === halfway,
  "Zoom-out should expose black outside the old image footprint"
);
// Reproduce a rapid two-step zoom-out: the first step exposes an empty ring, then the
// next histogram fills the wider view. That ring must let the new detail show through.
device.queue.writeBuffer(histogram, 0, new Uint32Array(size * size * 3).fill(10));
display.draw(settings);
check(display.preserve(), "Detailed initial frame should be retained");
const empty = device.createCommandEncoder();
empty.clearBuffer(histogram);
device.queue.submit([empty.finish()]);
display.draw(settings, { scale: [2, 2], offset: [-0.5, -0.5], opacity: 1 });
const intermediate = await read();
check(
  intermediate[0] === 0 && intermediate[3] === 0,
  "Empty intermediate ring must have zero coverage"
);
check(intermediate[256 + 4] === 255, "Original viewport should retain its detail");
check(display.preserve(), "Retain the intermediate zoom-out frame with its coverage");
device.queue.writeBuffer(histogram, 0, new Uint32Array(size * size * 3).fill(10));
display.draw(settings, { scale: [2, 2], offset: [-0.5, -0.5], opacity: 1 });
const wider = await read();
for (let y = 0; y < size; ++y) {
  for (let x = 0; x < size; ++x) {
    check(wider[y * 256 + x * 4] === 255, "Retained black ring must not hide newer detail");
  }
}
const blank = device.createCommandEncoder();
blank.clearBuffer(histogram);
device.queue.submit([blank.finish()]);
display.draw(settings);
check((await read())[0] === 0, "Completed fade should show only the new histogram");
const validation = await device.popErrorScope();
if (validation) throw new Error(validation.message);
target.destroy();
device.destroy();
globalThis.console.log(
  "PASS: real WebGPU shader, retained frame, half fade, repeated zoom, transparent zoom-out bands, clipping and clean export"
);
