// Buddha++ explorer: Metropolis sampling and display entirely in the browser on WebGPU. Find a
// region here, export it, then render it at full size with the buddha++ command line.
import "./style.css";
import { Display, type DisplaySettings } from "./display";
import { loadExclusionMap } from "./exclusion";
import { element, field } from "./form";
import { attachGeometry, fitPixels, observeDevicePixels, sameSize, type Size } from "./geometry";
import { encodePng } from "./png";
import { Sampler, type Metrics, type SamplerSettings } from "./sampler";
import { decodeHash, encodeHash } from "./share";
import displaySource from "./shaders/display.wgsl?raw";
import metropolisSource from "./shaders/metropolis.wgsl?raw";

// Preview refresh interval while sampling; display changes redraw on the next frame.
const PREVIEW_MS = 100;
// The URL hash follows the settings once they have been still this long, as browsers throttle
// history updates.
const HASH_MS = 300;
// Complex units across the shorter side of the viewport in the full view.
const FULL_SPAN = 4;
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

const samplerNumbers = [
  "cre",
  "cim",
  "span",
  "lowr",
  "highr",
  "lowg",
  "highg",
  "lowb",
  "highb",
  "radius",
  "exponent_l",
  "exponent_c",
  "chain_scale",
] as const;
// Seeding modes and samplers, in the order of the shader's p.seeding and p.sampler.
const seedings = ["walk", "uniform", "newton"];
const samplers = ["metropolis", "naive"];
const displayNames = ["exposure", "gamma", "saturation", "texture", "sharpness"] as const;
const defaults: Record<string, number | string> = {
  lowr: 512,
  highr: 8192,
  lowg: 128,
  highg: 2048,
  lowb: 32,
  highb: 512,
  radius: 1,
  exponent_l: 1,
  exponent_c: 1,
  chain_scale: 1,
  seeding: "newton",
  sampler: "metropolis",
  exposure: 0.2, // stops
  gamma: 0.3,
  saturation: 100,
  texture: 25,
  sharpness: 75,
};

// The view is always in the URL; other settings only when they differ from these.
const hashDefaults = { re: 0, im: 0, span: FULL_SPAN, ...defaults };
const viewKeys = ["re", "im", "span"];

const isDisplayName = (name: string) => (displayNames as readonly string[]).includes(name);

function fullViewPreset() {
  field(form, "cre").value = "0";
  field(form, "cim").value = "0";
  field(form, "span").value = String(FULL_SPAN);
  for (const [name, value] of Object.entries(defaults)) field(form, name).value = String(value);
  updateDisplayLabels();
  updateSamplerFields();
}

// The Metropolis settings do not apply to naive sampling.
function updateSamplerFields() {
  metropolisFields.disabled = field(form, "sampler").value === "naive";
}

function updateDisplayLabels() {
  for (const name of displayNames) {
    const value = Number(field(form, name).value);
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
  for (const input of form.querySelectorAll("input")) input.setCustomValidity("");
  for (const color of ["r", "g", "b"]) {
    const low = field(form, `low${color}`);
    const high = field(form, `high${color}`) as HTMLInputElement;
    if (Number(low.value) >= Number(high.value)) {
      high.setCustomValidity("Maximum must exceed minimum");
    }
  }
  return report ? form.reportValidity() : form.checkValidity();
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

async function startGpu() {
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
  device.lost.then((info) => {
    message.textContent = `GPU device lost: ${info.message}`;
  });
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
    sizeLimits: sizeLimits(device.limits),
    sampler: new Sampler(device, samplerModule, exclusion, timestamps),
    display: new Display(device, displayModule, context, format),
  };
}

let gpu: Awaited<ReturnType<typeof startGpu>>;
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

// The sampler fields by name, with the sampler and seeding mode as their option values. Shared by
// the URL hash and the PNG metadata.
function samplerValues() {
  const numbers = Object.fromEntries(
    samplerNumbers.map((name) => [name, Number(field(form, name).value)])
  ) as Record<(typeof samplerNumbers)[number], number>;
  return {
    ...numbers,
    sampler: field(form, "sampler").value,
    seeding: field(form, "seeding").value,
  };
}

// The sampler takes pixels per complex unit, derived from the span at the current size.
function samplerSettings(): SamplerSettings {
  const { span, ...settings } = Object.fromEntries(
    samplerNumbers.map((name) => [name, Number(field(form, name).value)])
  ) as Record<(typeof samplerNumbers)[number], number>;
  const seeding = Math.max(0, seedings.indexOf(field(form, "seeding").value));
  const sampler = Math.max(0, samplers.indexOf(field(form, "sampler").value));
  const scale = Math.min(width, height) / span;
  return { ...settings, scale, seeding, sampler, width, height };
}

function displaySettings(): DisplaySettings {
  return Object.fromEntries(
    displayNames.map((name) => [name, Number(field(form, name).value)])
  ) as unknown as DisplaySettings;
}

function renderSize() {
  return fitPixels(shown!, gpu.sizeLimits);
}

// Reallocates the histogram when the canvas size changes, then starts a new render.
function restart() {
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
  if (!validSettings(false)) {
    message.textContent = "Invalid settings";
    return;
  }
  const settings = samplerSettings();
  gpu.sampler.start(settings);
  pauseButton.textContent = "Pause";
  // Orbits are single precision, as in buddha-metal: pixels smaller than the float spacing
  // around |c| ~ 2 show blocky artifacts.
  message.textContent = 1 / settings.scale < 1e-6 ? "Beyond single precision" : "Running";
  displayDirty = displayForced = true;
  updateHashLater();
}

// Resets to the full view and defaults, then applies the settings in the URL hash. A value the
// form rejects, such as one out of range or an unknown seeding mode, keeps its default.
function applyHash() {
  fullViewPreset();
  validSettings(false); // clears custom errors left from before
  const { re, im, span, ...values } = decodeHash(location.hash, hashDefaults);
  const set = (name: string, value: number | string | undefined) => {
    if (value === undefined) return;
    const input = field(form, name);
    const before = input.value;
    input.value = String(value);
    // Range inputs clamp and select inputs blank values they cannot hold.
    if (input.value !== String(value) || !input.checkValidity()) input.value = before;
  };
  set("cre", re);
  set("cim", im);
  set("span", span);
  for (const [name, value] of Object.entries(values)) set(name, value);
  for (const color of ["r", "g", "b"]) {
    const [low, high] = [`low${color}`, `high${color}`];
    if (Number(field(form, low).value) >= Number(field(form, high).value)) {
      field(form, low).value = String(defaults[low]);
      field(form, high).value = String(defaults[high]);
    }
  }
  validSettings(false);
  updateDisplayLabels();
  updateSamplerFields();
}

function settingsHash() {
  const { cre, cim, ...values } = samplerValues();
  return encodeHash({ re: cre, im: cim, ...values, ...displaySettings() }, hashDefaults, viewKeys);
}

let hashTimer: ReturnType<typeof setTimeout> | undefined;

// Replaces the hash without a history entry; replaceState does not fire hashchange.
function updateHash() {
  clearTimeout(hashTimer);
  if (!validSettings(false)) return;
  const hash = settingsHash();
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
// settings, and how far sampling had got. Stored as JSON in the PNG. The window is the span, as
// in links; scale, pixels per complex unit at this size, stays for readers from before span.
function exportMetadata() {
  const m = gpu.sampler.metrics;
  const values = samplerValues();
  return {
    format: "buddha-explorer/1",
    width,
    height,
    ...values,
    scale: Math.min(width, height) / values.span,
    display: displaySettings(),
    orbits: m.seeds + m.proposals,
    points: m.increments,
    seconds: Number(gpu.sampler.elapsed().toFixed(1)),
  };
}

function download(blob: Blob, name: string) {
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

async function exportPng() {
  exportButton.disabled = true;
  try {
    const created = new Date();
    const settings = exportMetadata();
    const image = await gpu.display.capture(displaySettings());
    const png = await encodePng(image, {
      Software: "Buddha++ explorer",
      "Creation Time": created.toISOString(),
      Description:
        `Buddhabrot at ${settings.cre} ${settings.cim >= 0 ? "+" : "-"} ` +
        `${Math.abs(settings.cim)}i, span ${settings.span}, ` +
        `${formatSamples(settings.points)} points from ${formatSamples(settings.orbits)} orbits`,
      "buddha-explorer": JSON.stringify(settings),
    });
    const local = new Date(created.getTime() - created.getTimezoneOffset() * 60000);
    const name = `buddha-${local.toISOString().slice(0, 19).replace(/[:T]/g, "-")}.png`;
    download(png, name);
    message.textContent = `Saved ${name}`;
  } catch (error) {
    message.textContent = `Export failed: ${errorMessage(error)}`;
  } finally {
    exportButton.disabled = false;
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
    ({ m, elapsed }) => `${gpu.sampler.timestamps ? "" : "~"}${share(m.gpuSeconds, elapsed)}`,
  ],
  ["size", sizeText],
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
  const m = gpu.sampler.metrics;
  const progress = { m, orbits: m.seeds + m.proposals, elapsed: gpu.sampler.elapsed() };
  measures.forEach(([, text], i) => (measureValues[i].textContent = text(progress)));
}

function frame(now: number) {
  if (restartPending && shown) restart();
  if (displayDirty && (displayForced || now - lastDraw >= PREVIEW_MS)) {
    gpu.display.draw(displaySettings());
    lastDraw = now;
    displayDirty = displayForced = false;
  }
  if (now - lastMeasures >= MEASURES_MS) {
    lastMeasures = now;
    updateMeasures();
  }
  requestAnimationFrame(frame);
}

const restartLater = () => {
  restartPending = true;
};

// The first size starts the render; later ones restart it once they settle, and only if the
// histogram size changes. The view keeps its span, so the image shows the same region. Until the
// restart, the canvas is hidden rather than stretched to a shape the render was not made for.
function resized(size: Size) {
  const first = !shown;
  shown = size;
  clearTimeout(resizeTimer);
  if (first) {
    restartLater();
    return;
  }
  const changed = () => !sameSize(renderSize(), { width, height });
  if (changed()) canvas.style.visibility = "hidden";
  resizeTimer = setTimeout(() => {
    if (changed()) restartLater();
    else canvas.style.visibility = "";
  }, RESIZE_MS);
}

form.addEventListener("input", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (isDisplayName(name)) {
    updateDisplayLabels();
    displayDirty = displayForced = true;
  }
});
form.addEventListener("change", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (name === "sampler") updateSamplerFields();
  if (isDisplayName(name)) updateHashLater();
  else if (validSettings(true)) restartLater();
});
form.addEventListener("submit", (event) => event.preventDefault());

pauseButton.addEventListener("click", () => {
  if (gpu.sampler.running) {
    gpu.sampler.pause();
    pauseButton.textContent = "Resume";
    message.textContent = "Paused";
  } else {
    gpu.sampler.resume();
    pauseButton.textContent = "Pause";
    message.textContent = "Running";
  }
});
element("preset").addEventListener("click", () => {
  fullViewPreset();
  restartLater();
});
exportButton.addEventListener("click", exportPng);
// A link pasted into this tab only changes the hash, so apply it here.
addEventListener("hashchange", () => {
  applyHash();
  restartLater();
});

async function main() {
  applyHash();
  try {
    gpu = await startGpu();
    gpu.sampler.onBatch = () => {
      displayDirty = true;
    };
    attachGeometry({ form, viewport, validSettings, viewChanged: restartLater });
    observeDevicePixels(canvas, resized);
    requestAnimationFrame(frame);
  } catch (error) {
    message.textContent = errorMessage(error);
  }
}

main();
