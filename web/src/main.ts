// Buddha++ explorer: Metropolis sampling and display entirely in the browser on WebGPU. Find a
// region here, export it, then render it at full size with the buddha++ command line.
import "./style.css";
import { Display } from "./display";
import type { EncodeRequest } from "./encode-worker";
import { loadExclusionMap } from "./exclusion";
import { element, field } from "./form";
import { attachGeometry, fitPixels, observeDevicePixels, sameSize, type Size } from "./geometry";
import { Sampler, type Metrics } from "./sampler";
import {
  configureControls,
  defaults,
  displayNames,
  displaySettings,
  isDisplayName,
  readSettings,
  samplerSettings,
  samplerValues,
  settingsFromHash,
  settingsHash,
  writeSettings,
  type Settings,
} from "./settings";
import displaySource from "./shaders/display.wgsl?raw";
import metropolisSource from "./shaders/metropolis.wgsl?raw";

// Preview refresh interval while sampling; display changes redraw on the next frame.
const PREVIEW_MS = 100;
// The URL hash follows the settings once they have been still this long, as browsers throttle
// history updates.
const HASH_MS = 300;
// A new canvas size restarts the render once it has been still this long, so dragging a window
// edge restarts once rather than on every step.
const RESIZE_MS = 200;
// Refresh interval of the measures under the settings.
const MEASURES_MS = 250;

const form = element<HTMLFormElement>("controls");
const pauseButton = element<HTMLButtonElement>("pause");
const viewport = element<HTMLDivElement>("viewport");
const canvas = element<HTMLCanvasElement>("image");
const message = element<HTMLParagraphElement>("message");
const measureList = element<HTMLDListElement>("measures");
const exportButton = element<HTMLButtonElement>("export");
const metropolisFields = element<HTMLFieldSetElement>("metropolis");
const menuButton = element<HTMLButtonElement>("menu");
const closeButton = element<HTMLButtonElement>("close");

const retryButton = element<HTMLButtonElement>("retry");
let settings: Settings = { ...defaults };
// The sampler settings that actually produced the current histogram. Draft form edits and
// pending restarts must never describe a different image in export metadata.
let renderedSettings: Settings | undefined;
let paused = false;
let phase: "loading" | "ready" | "failed" = "loading";
let gpuGeneration = 0;
let frameId = 0;
let encoding = false;

function showSettings() {
  writeSettings(form, settings);
  updateDisplayLabels();
  updateSamplerFields();
}

// The Metropolis settings do not apply to naive sampling.
function updateSamplerFields() {
  metropolisFields.disabled = field(form, "sampler").value === "naive";
}

function updateDisplayLabels() {
  for (const name of displayNames) {
    const value = settings[name];
    const signed = (text: string) => (value > 0 ? `+${text}` : text);
    element(`${name}-value`).textContent =
      name === "exposure"
        ? signed(value.toFixed(2))
        : name === "gamma"
          ? value.toFixed(2)
          : signed(String(value));
  }
}

function validSettings(report: boolean) {
  return readSettings(form, settings, report) !== undefined;
}

function commitSettings(report = false) {
  const next = readSettings(form, settings, report);
  if (!next) return false;
  settings = next;
  restartLater();
  updateHashLater();
  return true;
}

function updateActions() {
  pauseButton.textContent = paused ? "Resume" : "Pause";
  pauseButton.disabled = phase !== "ready" || !renderedSettings;
  exportButton.disabled =
    phase !== "ready" ||
    !renderedSettings ||
    restartPending ||
    encoding ||
    canvas.style.visibility === "hidden";
  retryButton.hidden = phase !== "failed";
}

function renderStatus() {
  if (paused) return "Paused";
  if (renderedSettings && Math.min(width, height) / renderedSettings.span > 1e6) {
    return "Beyond single precision";
  }
  return "Running";
}

function failGpu(error: unknown, generation: number) {
  if (generation !== gpuGeneration || phase === "failed") return;
  phase = "failed";
  cancelAnimationFrame(frameId);
  clearTimeout(resizeTimer);
  gpu?.sampler.pause();
  message.textContent = errorMessage(error);
  updateActions();
  // Otherwise a phone shows a blank screen, with the reason hidden in the closed panel.
  setMenu(true);
}

function formatSamples(samples: number) {
  const units = ["", "K", "M", "G", "T", "P"];
  let unit = 0;
  while (samples >= 1000 && unit < units.length - 1) {
    samples /= 1000;
    ++unit;
  }
  samples = Number(samples.toPrecision(3));
  if (samples >= 1000 && unit < units.length - 1) {
    samples /= 1000;
    ++unit;
  }
  return `${samples}${units[unit]}`;
}

function format3(value: number) {
  return `${Number(value.toPrecision(3))}`;
}

function perSecond(amount: number, seconds: number) {
  return seconds > 0 ? formatSamples(amount / seconds) : "0";
}

// A percentage, or a dash when there is nothing to divide by (no proposals in naive sampling).
function share(part: number, whole: number) {
  return whole ? `${format3((100 * part) / whole)}%` : "–";
}

function errorMessage(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}

async function shaderModule(device: GPUDevice, name: string, code: string) {
  const module = device.createShaderModule({ label: name, code });
  const errors = (await module.getCompilationInfo()).messages.filter((m) => m.type === "error");
  if (errors.length) throw new Error(`${name}:${errors[0].lineNum}: ${errors[0].message}`);
  return module;
}

// The largest histogram the device can render. The largest display buffer takes 16 bytes per
// pixel (the histogram 12), each display pass dispatches one 256-lane workgroup per 256 pixels
// along one axis, and the canvas and captures are textures of the histogram's size.
function sizeLimits(limits: GPUSupportedLimits) {
  const bytes = Math.min(limits.maxStorageBufferBindingSize, limits.maxBufferSize);
  return {
    pixels: Math.min(Math.floor(bytes / 16), limits.maxComputeWorkgroupsPerDimension * 256),
    side: limits.maxTextureDimension2D,
  };
}

// The adapter as the browser describes it, such as "apple metal-3"; browsers may leave out details.
function adapterName(info: GPUAdapterInfo) {
  const parts = [info.vendor, info.architecture, info.device, info.description].filter(Boolean);
  return [...new Set(parts)].join(" ") || "–";
}

async function startGpu(onFailure: (error: unknown) => void) {
  if (!navigator.gpu) throw new Error("WebGPU is not available in this browser");
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error("No WebGPU adapter");
  const timestamps = adapter.features.has("timestamp-query");
  const device = await adapter.requestDevice({
    requiredFeatures: timestamps ? ["timestamp-query"] : [],
    requiredLimits: {
      maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
      maxBufferSize: adapter.limits.maxBufferSize,
      maxTextureDimension2D: adapter.limits.maxTextureDimension2D,
    },
  });
  device.lost.then((info) =>
    onFailure(new Error(`GPU device lost: ${info.message || info.reason}`))
  );
  device.addEventListener("uncapturederror", (event) => onFailure(event.error));
  try {
    const context = canvas.getContext("webgpu");
    if (!context) throw new Error("Cannot create a WebGPU canvas context");
    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format });
    const [samplerModule, displayModule, exclusion] = await Promise.all([
      shaderModule(device, "metropolis.wgsl", metropolisSource),
      shaderModule(device, "display.wgsl", displaySource),
      loadExclusionMap(device),
    ]);
    return {
      device,
      name: adapterName(adapter.info),
      sizeLimits: sizeLimits(device.limits),
      sampler: new Sampler(device, samplerModule, exclusion, timestamps),
      display: new Display(device, displayModule, context, format),
    };
  } catch (error) {
    onFailure(error);
    device.destroy();
    throw error;
  }
}

let gpu: Awaited<ReturnType<typeof startGpu>> | undefined;
// The canvas on screen in device pixels, once measured, and the histogram size: the same, unless
// the screen is larger than the GPU limits allow or a resize is still settling.
let shown: Size | undefined;
let width = 0;
let height = 0;
let resizeTimer: ReturnType<typeof setTimeout> | undefined;
let restartPending = true;
let displayDirty = true;
let displayForced = true;
let lastDraw = 0;
let lastMeasures = 0;

function renderSize() {
  return fitPixels(shown!, gpu!.sizeLimits);
}

// Reallocates the histogram when the canvas size changes, then starts a new render.
function restart() {
  if (!gpu || !shown || phase !== "ready") return;
  restartPending = false;
  clearTimeout(resizeTimer);
  const size = renderSize();
  if (!sameSize(size, { width, height })) {
    width = canvas.width = size.width;
    height = canvas.height = size.height;
    const histogram = gpu.device.createBuffer({
      size: width * height * 12,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    gpu.sampler.setHistogram(histogram);
    gpu.display.setHistogram(histogram, width, height);
  }
  canvas.style.visibility = "";
  gpu.sampler.start(samplerSettings(settings, width, height), !paused);
  if (phase !== "ready") return;
  renderedSettings = { ...settings };
  gpu.display.mirrorAxis = gpu.sampler.mirrorAxis;
  message.textContent = renderStatus();
  displayDirty = displayForced = true;
  updateActions();
}

function applyHash() {
  settings = settingsFromHash(location.hash);
  showSettings();
  updateHashLater();
}

let hashTimer: ReturnType<typeof setTimeout> | undefined;

// Replaces the hash without a history entry; replaceState does not fire hashchange.
function updateHash() {
  clearTimeout(hashTimer);
  const hash = settingsHash(settings);
  if (hash === location.hash) return;
  try {
    history.replaceState(history.state, "", hash);
  } catch {
    // Too many updates; the next change tries again.
  }
}

function updateHashLater() {
  clearTimeout(hashTimer);
  hashTimer = setTimeout(updateHash, HASH_MS);
}

// Everything needed to reproduce the exported image: the view, the sampler and display
// settings, and how far sampling had got. Stored as JSON in the image. The window is the span, as
// in links; scale, pixels per complex unit at this size, stays for readers from before span.
function exportMetadata() {
  const m = gpu!.sampler.metrics;
  const values = samplerValues(renderedSettings!);
  return {
    format: "buddha-explorer/1",
    width,
    height,
    ...values,
    scale: Math.min(width, height) / values.span,
    display: displaySettings(settings),
    orbits: m.seeds + m.proposals,
    points: m.increments,
    seconds: Number(gpu!.sampler.elapsed().toFixed(1)),
  };
}

function download(blob: Blob, name: string) {
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

// Encodes the image in a worker, one per export, as lossless WebP or PNG.
function encodeImage(request: EncodeRequest) {
  const worker = new Worker(new URL("./encode-worker.ts", import.meta.url), { type: "module" });
  return new Promise<Blob>((resolve, reject) => {
    worker.onmessage = (event: MessageEvent<Blob>) => resolve(event.data);
    worker.onerror = (event) => reject(new Error(event.message || "Encoding failed"));
    worker.postMessage(request, [request.image.data.buffer]);
  }).finally(() => worker.terminate());
}

async function exportImage() {
  if (!gpu || exportButton.disabled) return;
  const generation = gpuGeneration;
  encoding = true;
  updateActions();
  message.textContent = "Encoding…";
  try {
    const created = new Date();
    const settings = exportMetadata();
    const image = await gpu.display.capture(settings.display);
    const text = {
      Software: "Buddha++ explorer",
      "Creation Time": created.toISOString(),
      Description:
        `Buddhabrot at ${settings.cre} ${settings.cim >= 0 ? "+" : "-"} ` +
        `${Math.abs(settings.cim)}i, span ${settings.span}, ` +
        `${formatSamples(settings.points)} points from ${formatSamples(settings.orbits)} orbits`,
      "buddha-explorer": JSON.stringify(settings),
    };
    const blob = await encodeImage({ image, text });
    const local = new Date(created.getTime() - created.getTimezoneOffset() * 60000);
    const name = `buddha-${local.toISOString().slice(0, 19).replace(/[:T]/g, "-")}.${blob.type === "image/webp" ? "webp" : "png"}`;
    download(blob, name);
    if (generation === gpuGeneration && phase === "ready") message.textContent = `Saved ${name}`;
  } catch (error) {
    if (generation === gpuGeneration && phase === "ready") {
      message.textContent = `Export failed: ${errorMessage(error)}`;
    }
  } finally {
    encoding = false;
    updateActions();
  }
}

interface Progress {
  m: Metrics;
  orbits: number;
  elapsed: number; // wall-clock seconds
}

// The measures under the settings, one per line: the image's progress (histogram points and
// orbits, counted as the Metal sampler counts them), a few diagnostics, then its size.
const measures: [string, (p: Progress) => string][] = [
  ["points", ({ m }) => formatSamples(m.increments)],
  ["points/s", ({ m, elapsed }) => perSecond(m.increments, elapsed)],
  ["orbits", ({ orbits }) => formatSamples(orbits)],
  ["orbits/s", ({ orbits, elapsed }) => perSecond(orbits, elapsed)],
  // How well mutations are sized: near 100% they barely move, low and most are wasted.
  ["accepted", ({ m }) => share(m.accepted, m.proposals)],
  // Orbits that reached the image, of all computed (seed attempts and proposals).
  ["drawn", ({ m, orbits }) => share(m.escaped, orbits)],
  // Seed attempts per chain started: the cost of finding seeds, which Newton seeding cuts.
  ["seeds/chain", ({ m }) => (m.chains ? format3(m.seeds / m.chains) : "–")],
  // GPU time over wall-clock time while running; estimated (~) without timestamp queries.
  [
    "GPU busy",
    ({ m, elapsed }) => `${gpu!.sampler.timestamps ? "" : "~"}${share(m.gpuSeconds, elapsed)}`,
  ],
  ["size", sizeText],
  ["GPU", () => gpu!.name],
];

// The histogram size, and the canvas's on screen when they differ: the screen exceeds the GPU
// limits and the image is scaled up, keeping its proportions, or a resize has not settled yet and
// the canvas is hidden.
function sizeText() {
  const size = `${width}×${height}`;
  return shown && !sameSize(shown, { width, height })
    ? `${size} of ${shown.width}×${shown.height}`
    : size;
}

const measureValues = measures.map(([label]) => {
  const term = document.createElement("dt");
  const value = document.createElement("dd");
  term.textContent = label;
  measureList.append(term, value);
  return value;
});

function updateMeasures() {
  const m = gpu!.sampler.metrics;
  const progress = { m, orbits: m.seeds + m.proposals, elapsed: gpu!.sampler.elapsed() };
  measures.forEach(([, text], i) => (measureValues[i].textContent = text(progress)));
}

function frame(now: number) {
  if (phase !== "ready" || !gpu) return;
  try {
    if (restartPending && shown) restart();
    if (displayDirty && (displayForced || now - lastDraw >= PREVIEW_MS)) {
      gpu.display.draw(displaySettings(settings));
      lastDraw = now;
      displayDirty = displayForced = false;
    }
    if (now - lastMeasures >= MEASURES_MS) {
      lastMeasures = now;
      updateMeasures();
    }
    frameId = requestAnimationFrame(frame);
  } catch (error) {
    failGpu(error, gpuGeneration);
  }
}

const restartLater = () => {
  restartPending = true;
  updateActions();
};

// The first size starts the render; later ones restart it once they settle, and only if the
// histogram size changes. The view keeps its span, so the image shows the same region. Until the
// restart, the canvas is hidden rather than stretched to a shape the render was not made for.
function resized(size: Size) {
  const first = !shown;
  shown = size;
  clearTimeout(resizeTimer);
  if (phase !== "ready") return;
  if (first) {
    restartLater();
    return;
  }
  const changed = () => !sameSize(renderSize(), { width, height });
  if (changed()) canvas.style.visibility = "hidden";
  updateActions();
  resizeTimer = setTimeout(() => {
    if (changed()) restartLater();
    else canvas.style.visibility = "";
    updateActions();
  }, RESIZE_MS);
}

form.addEventListener("input", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (isDisplayName(name)) {
    settings = { ...settings, [name]: Number(field(form, name).value) };
    updateDisplayLabels();
    displayDirty = displayForced = true;
  }
});
form.addEventListener("change", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (name === "sampler") updateSamplerFields();
  if (isDisplayName(name)) updateHashLater();
  else commitSettings(true);
});
form.addEventListener("submit", (event) => event.preventDefault());

pauseButton.addEventListener("click", () => {
  if (phase !== "ready" || !gpu || !renderedSettings) return;
  paused = !paused;
  if (paused) gpu.sampler.pause();
  else gpu.sampler.resume();
  message.textContent = renderStatus();
  updateActions();
});
element("reset-view").addEventListener("click", () => {
  settings = { ...settings, cre: defaults.cre, cim: defaults.cim, span: defaults.span };
  showSettings();
  restartLater();
  updateHashLater();
});
element("reset-all").addEventListener("click", () => {
  settings = { ...defaults };
  showSettings();
  restartLater();
  updateHashLater();
});
retryButton.addEventListener("click", () => {
  if (phase === "failed") initializeGpu();
});
exportButton.addEventListener("click", exportImage);

// On small screens the panel is a full-screen overlay over the image, opened by the menu button
// over the image and closed by the one in its header; elsewhere the menu-open class has no effect.
function setMenu(open: boolean) {
  document.body.classList.toggle("menu-open", open);
  menuButton.setAttribute("aria-expanded", String(open));
}
menuButton.addEventListener("click", () => setMenu(true));
closeButton.addEventListener("click", () => setMenu(false));
addEventListener("keydown", (event) => {
  if (event.key === "Escape") setMenu(false);
});

// A link pasted into this tab only changes the hash, so apply it here.
addEventListener("hashchange", () => {
  applyHash();
  restartLater();
});

async function initializeGpu() {
  const generation = ++gpuGeneration;
  phase = "loading";
  cancelAnimationFrame(frameId);
  gpu?.sampler.pause();
  gpu?.device.destroy();
  gpu = undefined;
  Object.assign(window, { gpu });
  width = height = 0;
  renderedSettings = undefined;
  restartPending = displayDirty = displayForced = true;
  message.textContent = "Starting…";
  measureValues.forEach((value) => (value.textContent = "–"));
  updateActions();
  try {
    const ready = await startGpu((error) => failGpu(error, generation));
    if (generation !== gpuGeneration || phase !== "loading") {
      ready.sampler.pause();
      ready.device.destroy();
      return;
    }
    gpu = ready;
    // Kept for the existing experiments that drive the page through window.gpu.
    Object.assign(window, { gpu });
    gpu.sampler.onBatch = () => {
      displayDirty = true;
    };
    gpu.sampler.onError = (error) => failGpu(error, generation);
    phase = "ready";
    frameId = requestAnimationFrame(frame);
  } catch (error) {
    failGpu(error, generation);
  }
}

configureControls(form);
applyHash();
attachGeometry({
  form,
  viewport,
  validSettings,
  viewChanged: () => {
    commitSettings();
  },
});
observeDevicePixels(canvas, resized);
initializeGpu();
