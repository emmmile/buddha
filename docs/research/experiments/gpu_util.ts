// GPU utilization of the explorer (research doc, section 6). For each QUERY (a query string, or
// "" for none) it loads BASE/?QUERY#HASH in a visible Chrome window, waits WARM seconds, then for
// SECONDS samples IOAccelerator's "Device Utilization %" (what Activity Monitor and Stats show)
// and reads the explorer's counters through window.gpu. It prints one row per query: device
// utilization, the explorer's GPU busy share, points, orbits and orbit steps per second, GPU time
// per dispatch, steps per lane and the histogram size. CHROME_ARGS adds Chrome flags, such as
// --enable-unsafe-webgpu --enable-dawn-features=disable_robustness.
//
//   cd web && yarn dev --port 5190 --strictPort --host 127.0.0.1
//   deno run -A gpu_util.ts http://127.0.0.1:5190 '#re=0&im=0&span=4' 5 10 -- ""
//
// Close other GPU work first. It runs on the GPU for WARM + SECONDS per query.
const args = [...Deno.args];
const sep = args.indexOf("--");
const [base, hash, warmS, secS] = args.slice(0, sep);
const queries = args.slice(sep + 1);
const warm = Number(warmS),
  seconds = Number(secS);
const port = 9334;
const profile = await Deno.makeTempDir();
const chrome = new Deno.Command("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", {
  args: [
    `--remote-debugging-port=${port}`,
    "--window-size=1600,1000",
    `--user-data-dir=${profile}`,
    "--no-first-run",
    "--no-default-browser-check",
    ...(Deno.env.get("CHROME_ARGS")?.split(" ").filter(Boolean) ?? []),
    "about:blank",
  ],
  stdout: "null",
  stderr: "null",
}).spawn();
let version;
for (let i = 0; i < 100; i++) {
  try {
    version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
    break;
  } catch {
    await new Promise((r) => setTimeout(r, 200));
  }
}
const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let id = 0;
const pending = new Map<number, (v: any) => void>();
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) {
    pending.get(m.id)!(m);
    pending.delete(m.id);
  }
};
const send = (method: string, params: any = {}, sessionId?: string) =>
  new Promise<any>((r) => {
    const i = ++id;
    pending.set(i, r);
    ws.send(JSON.stringify({ id: i, method, params, sessionId }));
  });
const targets = (await send("Target.getTargets")).result.targetInfos;
const page = targets.find((t: any) => t.type === "page");
const {
  result: { sessionId },
} = await send("Target.attachToTarget", {
  targetId: page.targetId,
  flatten: true,
});
await send("Page.bringToFront", {}, sessionId);
const evaluate = async (expression: string) =>
  (
    await send(
      "Runtime.evaluate",
      { expression, awaitPromise: true, returnByValue: true },
      sessionId
    )
  ).result?.result?.value;

const ioreg = async () => {
  const out = new TextDecoder().decode(
    (await new Deno.Command("ioreg", { args: ["-r", "-c", "IOAccelerator", "-d", "1"] }).output())
      .stdout
  );
  const get = (k: string) => Number(out.match(new RegExp(`"${k}"=(\\d+)`))?.[1]);
  return { device: get("Device Utilization %"), renderer: get("Renderer Utilization %") };
};
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const snap = () =>
  evaluate(`(() => { const s = gpu.sampler; const m = s.metrics;
    return { inc: m.increments, orbits: m.seeds + m.proposals, it: m.iterations + m.redraw + m.solve,
      gpu: m.gpuSeconds, batches: m.batches, wall: s.elapsed(), steps: s.stepsPerLane,
      size: document.getElementById("measures")?.textContent?.match(/size(\\S+)/)?.[1] }; })()`);

console.log("query\tAM device%\tbusy%\tpoints/s\torbits/s\tsteps/s\tbatch ms\tsteps/lane\tsize");
for (const q of queries) {
  await send("Page.navigate", { url: "about:blank" }, sessionId);
  await sleep(1500);
  await send("Page.navigate", { url: `${base}/?${q}${hash}` }, sessionId);
  for (
    let i = 0;
    i < 100 && !(await evaluate("typeof gpu !== 'undefined' && gpu.sampler.running"));
    i++
  )
    await sleep(100);
  await sleep(warm * 1000);
  const a = await snap();
  const samples: number[] = [];
  const end = performance.now() + seconds * 1000;
  while (performance.now() < end) {
    samples.push((await ioreg()).device);
    await sleep(250);
  }
  const b = await snap();
  const wall = b.wall - a.wall;
  const avg = samples.reduce((x, y) => x + y, 0) / samples.length;
  const M = (x: number) => (x / wall / 1e6).toFixed(1) + "M";
  console.log(
    [
      q || "(default)",
      avg.toFixed(0),
      ((100 * (b.gpu - a.gpu)) / wall).toFixed(0),
      M(b.inc - a.inc),
      M(b.orbits - a.orbits),
      ((b.it - a.it) / wall / 1e9).toFixed(2) + "G",
      ((1000 * (b.gpu - a.gpu)) / (b.batches - a.batches)).toFixed(1),
      b.steps,
      b.size,
    ].join("\t")
  );
}
ws.close();
chrome.kill();
Deno.exit(0);
