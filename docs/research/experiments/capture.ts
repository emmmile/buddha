// Renders explorer views in headless Chrome and exports them with the explorer's own Export PNG,
// so the images go through its display pipeline and carry their settings. For each seeding mode it
// loads BASE/HASH&seeding=MODE, renders for SECONDS, prints the measures, and saves OUT/MODE.png
// (an existing file of that name is replaced).
//
//   cd web && yarn dev --port 5180 --strictPort --host 127.0.0.1
//   deno run -A capture.ts http://127.0.0.1:5180 '#re=0&im=0&span=4' 45 OUT walk uniform newton
//
// It drives Chrome over the DevTools protocol on port 9333, in a 1600 x 1000 window at pixel ratio
// 1. It runs on the GPU for SECONDS per mode.
const [base, hash, seconds, out, ...modes] = Deno.args;
const port = 9333;
const profile = await Deno.makeTempDir();
const chrome = new Deno.Command("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", {
  args: [
    "--headless=new",
    `--remote-debugging-port=${port}`,
    "--enable-unsafe-webgpu",
    "--window-size=1600,1000",
    "--force-device-scale-factor=1",
    `--user-data-dir=${profile}`,
    "--no-first-run",
    "--no-default-browser-check",
    "about:blank",
  ],
  stdout: "null",
  stderr: "null",
}).spawn();
let version;
for (let i = 0; i < 50; i++) {
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
const events: any[] = [];
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) {
    pending.get(m.id)!(m);
    pending.delete(m.id);
  } else events.push(m);
};
const send = (method: string, params: any = {}, sessionId?: string) =>
  new Promise<any>((r) => {
    const i = ++id;
    pending.set(i, r);
    ws.send(JSON.stringify({ id: i, method, params, sessionId }));
  });
const {
  result: { targetId },
} = await send("Target.createTarget", { url: "about:blank" });
const {
  result: { sessionId },
} = await send("Target.attachToTarget", { targetId, flatten: true });
await send("Browser.setDownloadBehavior", {
  behavior: "allow",
  downloadPath: out,
  eventsEnabled: true,
});
const evaluate = async (expression: string) =>
  (
    await send(
      "Runtime.evaluate",
      { expression, awaitPromise: true, returnByValue: true },
      sessionId
    )
  ).result?.result?.value;
for (const mode of modes) {
  await send("Page.navigate", { url: "about:blank" }, sessionId);
  await new Promise((r) => setTimeout(r, 500));
  await send("Page.navigate", { url: `${base}/${hash}&seeding=${mode}` }, sessionId);
  await new Promise((r) => setTimeout(r, Number(seconds) * 1000));
  const measures = await evaluate(
    `[...document.querySelectorAll('#measures dt')].map(d => d.textContent + ' ' + d.nextElementSibling.textContent).join(' | ') + ' || ' + document.getElementById('message').textContent`
  );
  events.length = 0;
  await evaluate(`document.getElementById('export').click()`);
  let name = "";
  for (let i = 0; i < 200 && !name; i++) {
    await new Promise((r) => setTimeout(r, 100));
    const begin = events.find((e) => e.method === "Browser.downloadWillBegin");
    const done = events.find(
      (e) => e.method === "Browser.downloadProgress" && e.params.state === "completed"
    );
    if (begin && done) name = begin.params.suggestedFilename;
  }
  if (name) await Deno.rename(`${out}/${name}`, `${out}/${mode}.png`);
  console.log(`${mode}: ${name ? mode + ".png" : "EXPORT FAILED"} :: ${measures}`);
}
ws.close();
chrome.kill();
