// Runs the explorer's Metropolis sampler headless (4,096 lanes, a 256 x 256 window) and prints the
// counters per seeding mode, as in the research doc's GPU smoke test. Needs the Newton seeding
// shader (MR !3). Run with `deno run -A smoke.ts`.
import { decompress } from "npm:fzstd@^0.1.1";
const root = new URL("../../../", import.meta.url); // the repository

const code = await Deno.readTextFile(new URL("web/src/shaders/metropolis.wgsl", root));
const adapter = await navigator.gpu.requestAdapter();
const device = await adapter!.requestDevice();
const bytes = decompress(await Deno.readFile(new URL("data/exclusion.map", root)));
const size = Number(new DataView(bytes.buffer, bytes.byteOffset).getBigUint64(16, true));
const bits = bytes.slice(24, 24 + (size * size) / 16);
const rows = size / 2;
const ex = (x: number, y: number) => {
  if (x < 0 || x >= size || y < 0) return false;
  const c = Math.min(y, rows - 1) * size + x;
  return (bits[c >> 3] & (1 << (c & 7))) !== 0;
};
const list: number[] = [];
for (let y = 0; y < rows; y++)
  for (let x = 0; x < size; x++)
    if (!ex(x, y) && (ex(x - 1, y) || ex(x + 1, y) || ex(x, y - 1) || ex(x, y + 1)))
      list.push(y * size + x);
const boundary = new Uint32Array(list);
const buf = (data: ArrayBufferView | number, usage: number) => {
  const b = device.createBuffer({
    size: typeof data === "number" ? data : Math.max(4, data.byteLength),
    usage: usage | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
  });
  if (typeof data !== "number") device.queue.writeBuffer(b, 0, data as BufferSource);
  return b;
};
const THREADS = 4096,
  W = 256,
  H = 256;
const module = device.createShaderModule({ code });
const bgl = device.createBindGroupLayout({
  entries: [0, 1, 2, 3, 4, 5].map((binding) => ({
    binding,
    visibility: GPUShaderStage.COMPUTE,
    buffer: { type: binding === 0 ? "uniform" : binding >= 4 ? "read-only-storage" : "storage" },
  })),
});
const layout = device.createPipelineLayout({ bindGroupLayouts: [bgl] });
const begin = device.createComputePipeline({ layout, compute: { module, entryPoint: "begin" } });
const metro = device.createComputePipeline({
  layout,
  compute: { module, entryPoint: "metropolis" },
});
const params = buf(96, GPUBufferUsage.UNIFORM);
const hist = buf(W * H * 12, GPUBufferUsage.STORAGE);
const chains = buf(THREADS * 104, GPUBufferUsage.STORAGE);
const stats = buf(36, GPUBufferUsage.STORAGE);
const cells = buf(bits, GPUBufferUsage.STORAGE);
const bnd = buf(boundary, GPUBufferUsage.STORAGE);
const group = device.createBindGroup({
  layout: bgl,
  entries: [params, hist, chains, stats, cells, bnd].map((buffer, binding) => ({
    binding,
    resource: { buffer },
  })),
});
const names = [
  "seeds",
  "proposals",
  "accepted",
  "chains",
  "escaped",
  "increments",
  "iterations",
  "redraw",
  "solve",
];
async function run(
  seeding: number,
  cre: number,
  cim: number,
  span: number,
  dispatches: number,
  steps: number
) {
  const scale = W / span;
  const u = new Uint32Array(24),
    f = new Float32Array(u.buffer);
  u.set([
    512,
    8192,
    128,
    2048,
    32,
    512,
    32,
    8192,
    W,
    H,
    THREADS,
    steps,
    seeding,
    12345,
    size,
    boundary.length,
  ]);
  f.set([cre - H / 2 / scale, cim - W / 2 / scale, scale, (0.01 * W) / scale, 1, 1, 1], 16);
  device.queue.writeBuffer(params, 0, u);
  const total = new Array(9).fill(0);
  for (let d = 0; d <= dispatches; d++) {
    const enc = device.createCommandEncoder();
    if (d === 0) enc.clearBuffer(hist);
    enc.clearBuffer(stats);
    const pass = enc.beginComputePass();
    pass.setPipeline(d === 0 ? begin : metro);
    pass.setBindGroup(0, group);
    pass.dispatchWorkgroups(THREADS / 64);
    pass.end();
    const rb = device.createBuffer({
      size: 36,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });
    enc.copyBufferToBuffer(stats, 0, rb, 0, 36);
    device.queue.submit([enc.finish()]);
    await rb.mapAsync(GPUMapMode.READ);
    new Uint32Array(rb.getMappedRange()).forEach((v, i) => (total[i] += d ? v : 0));
    rb.unmap();
  }
  const m = Object.fromEntries(names.map((n, i) => [n, total[i]]));
  const work = m.iterations + m.redraw + m.solve;
  console.log(
    `seeding ${["walk", "uniform", "newton"][seeding].padEnd(7)} span ${span.toExponential(0)}: chains ${m.chains}, seeds/chain ${(m.seeds / Math.max(1, m.chains)).toFixed(1)}, solve steps/chain ${(m.solve / Math.max(1, m.chains)).toFixed(0)}, drawn ${m.escaped}, increments ${m.increments}, increments per 1k steps ${((1000 * m.increments) / Math.max(1, work)).toFixed(2)}`
  );
}
console.log("boundary cells", boundary.length);
for (const span of [1e-2, 1e-3, 2e-4])
  for (const s of [0, 1, 2]) await run(s, -0.1, 0.75, span, 8, 2048);
