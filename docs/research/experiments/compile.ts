// Compiles web/src/shaders/metropolis.wgsl and creates both pipelines with the six-binding layout,
// to catch WGSL errors without a browser. Run with `deno run -A compile.ts`.
const root = new URL("../../../", import.meta.url); // the repository
const code = await Deno.readTextFile(new URL("web/src/shaders/metropolis.wgsl", root));
const adapter = await navigator.gpu.requestAdapter();
const device = await adapter!.requestDevice();
device.pushErrorScope("validation");
const module = device.createShaderModule({ code });
const info = await module.getCompilationInfo();
for (const m of info.messages) console.log(m.type, m.lineNum, m.message);
const layout = device.createBindGroupLayout({ entries: [0,1,2,3,4,5].map((binding) => ({ binding, visibility: GPUShaderStage.COMPUTE, buffer: { type: binding === 0 ? "uniform" : binding >= 4 ? "read-only-storage" : "storage" } })) });
const pl = device.createPipelineLayout({ bindGroupLayouts: [layout] });
for (const entryPoint of ["begin", "metropolis"]) device.createComputePipeline({ layout: pl, compute: { module, entryPoint } });
const err = await device.popErrorScope();
console.log(err ? "ERROR: " + err.message : "ok: compiled, both pipelines created");
