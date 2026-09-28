// HTTP protocol and request ordering for the local renderer.
let commandQueue = Promise.resolve();
export async function command(path, body) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}
export function enqueueCommand(path, body) {
  const next = commandQueue.then(() => command(path, body));
  commandQueue = next.catch(() => {});
  return next;
}
export async function getStatus() {
  const response = await fetch("/status", { cache: "no-store" });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}
export async function getFrame(revision) {
  const response = await fetch(`/frame.rgba?revision=${revision}`, {
    cache: "no-store",
  });
  if (response.status === 409) return null;
  if (!response.ok) throw new Error(await response.text());
  return new Uint8ClampedArray(await response.arrayBuffer());
}
