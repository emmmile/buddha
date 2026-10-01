// Buddha++ explorer: Metropolis sampling and display entirely in the browser on WebGPU. Find a
// region here, then render it at full size with the buddha++ command line.
import "./style.css";
import { Display, type DisplaySettings } from "./display";
import { loadExclusionMap } from "./exclusion";
import { element, field } from "./form";
import { attachGeometry, viewportPixelsFor } from "./geometry";
import { Sampler, type SamplerSettings } from "./sampler";
import displaySource from "./shaders/display.wgsl?raw";
import metropolisSource from "./shaders/metropolis.wgsl?raw";

// Preview refresh interval while sampling; display changes redraw on the next frame.
const PREVIEW_MS = 100;

const form = element<HTMLFormElement>("controls");
const pauseButton = element<HTMLButtonElement>("pause");
const viewport = element<HTMLDivElement>("viewport");
const canvas = element<HTMLCanvasElement>("image");
const message = element<HTMLSpanElement>("message");
const statusLine = element<HTMLSpanElement>("status");
const debugLine = element<HTMLDivElement>("debug-status");
const debugToggle = element<HTMLInputElement>("debug");
const commandBox = element<HTMLTextAreaElement>("command");

const samplerNumbers = [
  "cre",
  "cim",
  "scale",
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
  seeding: "walk",
  exposure: 1.5,
  gamma: 0.5,
  saturation: 0,
  texture: 0,
  sharpness: 0,
};

const isDisplayName = (name: string) => (displayNames as readonly string[]).includes(name);

function fullViewPreset() {
  const { width, height } = viewportPixelsFor(viewport);
  field(form, "cre").value = "0";
  field(form, "cim").value = "0";
  field(form, "scale").value = String(Math.min(width, height) / 4);
  for (const [name, value] of Object.entries(defaults)) field(form, name).value = String(value);
  updateDisplayLabels();
}

function updateDisplayLabels() {
  for (const name of displayNames) {
    const value = Number(field(form, name).value);
    element(`${name}-value`).textContent =
      name === "exposure" || name === "gamma"
        ? value.toFixed(2)
        : value > 0
          ? `+${value}`
          : `${value}`;
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

function share(part: number, whole: number) {
  return whole ? format3((100 * part) / whole) : "0";
}

function errorMessage(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}

function setDebug(enabled: boolean) {
  debugToggle.checked = enabled;
  debugLine.hidden = !enabled;
  try {
    localStorage.setItem("debug", enabled ? "1" : "0");
  } catch {
    // Storage may be unavailable; the toggle still works for this page.
  }
}

async function shaderModule(device: GPUDevice, name: string, code: string) {
  const module = device.createShaderModule({ label: name, code });
  const errors = (await module.getCompilationInfo()).messages.filter((m) => m.type === "error");
  if (errors.length) throw new Error(`${name}:${errors[0].lineNum}: ${errors[0].message}`);
  return module;
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
    sampler: new Sampler(device, samplerModule, exclusion, timestamps),
    display: new Display(device, displayModule, context, format),
  };
}

let gpu: Awaited<ReturnType<typeof startGpu>>;
let width = 0;
let height = 0;
let restartPending = true;
let displayDirty = true;
let displayForced = true;
let lastDraw = 0;
let lastStatus = 0;

function samplerSettings(): SamplerSettings {
  const settings = Object.fromEntries(
    samplerNumbers.map((name) => [name, Number(field(form, name).value)])
  ) as Record<(typeof samplerNumbers)[number], number>;
  const seeding = field(form, "seeding").value === "uniform" ? 1 : 0;
  return { ...settings, seeding, width, height };
}

function displaySettings(): DisplaySettings {
  return Object.fromEntries(
    displayNames.map((name) => [name, Number(field(form, name).value)])
  ) as unknown as DisplaySettings;
}

// Reallocates the histogram when the viewport size changes, then starts a new render.
function restart() {
  restartPending = false;
  const size = viewportPixelsFor(viewport);
  if (size.width !== width || size.height !== height) {
    width = canvas.width = size.width;
    height = canvas.height = size.height;
    const histogram = gpu.device.createBuffer({
      size: width * height * 12,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    gpu.sampler.setHistogram(histogram);
    gpu.display.setHistogram(histogram, width, height);
  }
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
}

// The buddha++ command that renders the current window. buddha++'s width runs along Re, the
// explorer's vertical axis, and its TIFF is rotated like the explorer, so the image matches the
// view. Exposure and gamma map to --lightness and --contrast of core/settings.cpp.
function renderCommand() {
  const s = samplerSettings();
  const d = displaySettings();
  const longSide = Number(field(form, "render_size").value);
  if (!width || !(longSide > 0) || !validSettings(false)) return "";
  const factor = longSide / Math.max(width, height);
  const scale = s.scale * factor;
  const clamp = (value: number) => Math.max(0, Math.min(200, Math.round(value)));
  const contrast = clamp((d.gamma * 200) / 0.7);
  // Exposure is ln(scale) * 70 * L / 256, with L = lightness / (201 - lightness) / 2.
  const twiceL = (2 * d.exposure * 256) / (70 * Math.log(Math.max(scale, 2)));
  return [
    "./build/buddha++ --sampler metropolis",
    `--cre ${s.cre} --cim ${s.cim} --scale ${Number(scale.toPrecision(15))}`,
    `--width ${Math.round(height * factor)} --height ${Math.round(width * factor)}`,
    `--red-min ${s.lowr} --red-max ${s.highr}`,
    `--green-min ${s.lowg} --green-max ${s.highg}`,
    `--blue-min ${s.lowb} --blue-max ${s.highb}`,
    `--contrast ${contrast} --lightness ${clamp((201 * twiceL) / (1 + twiceL))}`,
    "--out explorer",
  ].join(" \\\n  ");
}

// Metrics common to every sampler, then the Metropolis ones, as the native browser showed them.
function statusText() {
  const m = gpu.sampler.metrics;
  const elapsed = gpu.sampler.elapsed();
  const orbits = m.seeds + m.proposals;
  return [
    gpu.sampler.running ? "running" : "paused",
    `${width}x${height}`,
    `${formatSamples(orbits)} orbits`,
    `${perSecond(orbits, elapsed)} orbits/s`,
    `${perSecond(m.increments, elapsed)} points/s`,
    `${share(m.accepted, m.proposals)}% accepted`,
  ].join(" · ");
}

function debugText() {
  const m = gpu.sampler.metrics;
  const orbits = m.seeds + m.proposals;
  const estimated = gpu.sampler.timestamps ? "" : " (estimated)";
  return [
    "metropolis sampler",
    `${m.batches} batches`,
    `GPU ${perSecond(orbits, m.gpuSeconds)} orbits/s${estimated}`,
    `${share(m.escaped, orbits)}% drawn`,
    `${orbits ? format3((m.iterations + m.redraw) / orbits) : 0} steps/orbit`,
    `${m.chains ? format3(m.proposals / m.chains) : 0} proposals chain`,
    `${gpu.sampler.stepsPerLane} steps/lane`,
  ].join(" · ");
}

function frame(now: number) {
  if (restartPending) restart();
  if (displayDirty && (displayForced || now - lastDraw >= PREVIEW_MS)) {
    gpu.display.draw(displaySettings());
    lastDraw = now;
    displayDirty = displayForced = false;
  }
  if (now - lastStatus >= 250) {
    lastStatus = now;
    statusLine.textContent = statusText();
    debugLine.textContent = debugText();
    commandBox.value = renderCommand();
  }
  requestAnimationFrame(frame);
}

const restartLater = () => {
  restartPending = true;
};

form.addEventListener("input", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (isDisplayName(name)) {
    updateDisplayLabels();
    displayDirty = displayForced = true;
  }
});
form.addEventListener("change", (event) => {
  const name = (event.target as HTMLInputElement).name;
  if (!isDisplayName(name) && name !== "render_size" && validSettings(true)) restartLater();
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
element("copy").addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(renderCommand());
    message.textContent = "Command copied";
  } catch (error) {
    message.textContent = errorMessage(error);
  }
});
debugToggle.addEventListener("change", () => setDebug(debugToggle.checked));

async function main() {
  try {
    setDebug(localStorage.getItem("debug") === "1");
  } catch {
    setDebug(false);
  }
  fullViewPreset();
  try {
    gpu = await startGpu();
    gpu.sampler.onBatch = () => {
      displayDirty = true;
    };
    attachGeometry({ form, viewport, validSettings, viewChanged: restartLater });
    requestAnimationFrame(frame);
  } catch (error) {
    message.textContent = errorMessage(error);
  }
}

main();
