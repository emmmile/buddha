import { enqueueCommand, getFrame, getStatus } from "./api.js";
import { attachGeometry, viewportPixelsFor } from "./geometry.js";

const form = document.getElementById("controls");
const startButton = document.getElementById("start");
const viewport = document.getElementById("viewport");
const image = document.getElementById("image");
const message = document.getElementById("message");
const statusLine = document.getElementById("status");
const debugLine = document.getElementById("debug-status");
const debugToggle = document.getElementById("debug");
let lastFrameRevision = 0;
let paintedRenderId = 0;
let paintedDisplayRevision = 0;
let activeRenderId = 0;
let frameLoading = false;
let renderTimer;
let requestNumber = 0;
let displayTimer;
let displayDirty = false;
let displayTouched = false;
let displayInitialized = false;
let renderDirty = false;
let currentPhase = "idle";
let refreshToken = 0;

function fullViewPreset() {
  const { width, height } = viewportPixelsFor(viewport);
  form.elements.cre.value = 0;
  form.elements.cim.value = 0;
  form.elements.scale.value = Math.min(width, height) / 4;
  for (
    const [name, value] of Object.entries({
      lowr: 512,
      highr: 8192,
      lowg: 128,
      highg: 2048,
      lowb: 32,
      highb: 512,
    })
  ) {
    form.elements[name].value = value;
  }
  for (
    const [name, value] of Object.entries({
      radius: 1,
      exponent_l: 1,
      exponent_c: 1,
      chain_scale: 1,
      seeding: "walk",
    })
  ) {
    form.elements[name].value = value;
  }
  updateSamplerSettings();
  for (
    const [name, value] of Object.entries({
      brightness: 25,
      contrast: 10,
      saturation: 50,
      clarity: 50,
      texture: 50,
    })
  ) {
    form.elements[name].value = value;
  }
  updateDisplayLabels();
}

function updateSamplerSettings() {
  document.getElementById("metropolis-settings").hidden =
    form.elements.sampler.value !== "metropolis";
}

function updateStartButton() {
  startButton.textContent =
    ["starting", "running", "pausing"].includes(currentPhase)
      ? "Pause"
      : "Start";
  startButton.disabled = ["pausing", "resuming", "stopping"].includes(
    currentPhase,
  );
}

const displayNames = [
  "brightness",
  "contrast",
  "saturation",
  "clarity",
  "texture",
];
function updateDisplayLabels() {
  for (
    const name of ["brightness", "contrast", "saturation", "clarity", "texture"]
  ) {
    const value = Number(form.elements[name].value);
    document.getElementById(`${name}-value`).textContent = value > 0
      ? `+${value}`
      : `${value}`;
  }
}

function formatSamples(samples) {
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

function format3(value) {
  return `${Number(value.toPrecision(3))}`;
}

function displayData() {
  return Object.fromEntries(
    displayNames.map((name) => [name, Number(form.elements[name].value)]),
  );
}

async function sendDisplay() {
  clearTimeout(displayTimer);
  displayDirty = false;
  try {
    const status = await enqueueCommand("/display", displayData());
    void refreshUntil((s) =>
      s.render_id !== status.render_id ||
      (paintedRenderId === status.render_id &&
        paintedDisplayRevision >= status.display_revision)
    );
    return true;
  } catch (error) {
    displayDirty = true;
    message.textContent = error.message;
    return false;
  }
}

function scheduleDisplay() {
  displayDirty = true;
  displayTouched = true;
  message.textContent = "Updating display…";
  clearTimeout(displayTimer);
  displayTimer = setTimeout(sendDisplay, 80);
}

form.addEventListener("input", (event) => {
  if (displayNames.includes(event.target.name)) {
    updateDisplayLabels();
    scheduleDisplay();
  } else {
    if (event.target.name === "sampler") updateSamplerSettings();
    renderDirty = true;
  }
});

function validSettings(report) {
  for (const input of form.querySelectorAll("input")) {
    input.setCustomValidity("");
  }
  for (const color of ["r", "g", "b"]) {
    const low = form.elements[`low${color}`];
    const high = form.elements[`high${color}`];
    if (Number(low.value) >= Number(high.value)) {
      high.setCustomValidity("Maximum must exceed minimum");
    }
  }
  return report ? form.reportValidity() : form.checkValidity();
}

async function startRender() {
  clearTimeout(renderTimer);
  if (!validSettings(true)) return;
  const currentRequest = ++requestNumber;
  if (displayDirty && !await sendDisplay()) return;
  if (currentRequest !== requestNumber) return;
  const data = Object.fromEntries(new FormData(form));
  for (
    const name of [
      "cre",
      "cim",
      "scale",
      "lowr",
      "lowg",
      "lowb",
      "highr",
      "highg",
      "highb",
      "radius",
      "exponent_l",
      "exponent_c",
      "chain_scale",
    ]
  ) {
    data[name] = Number(data[name]);
  }
  Object.assign(data, viewportPixelsFor(viewport));
  try {
    currentPhase = "starting";
    updateStartButton();
    const status = await enqueueCommand("/render", data);
    if (currentRequest === requestNumber) {
      const renderId = status.render_id;
      activeRenderId = renderId;
      currentPhase = status.phase;
      renderDirty = false;
      updateStartButton();
      message.textContent = "Starting render " + activeRenderId + "…";
      void refreshUntil((s) =>
        s.render_id !== renderId || paintedRenderId === renderId
      );
    }
  } catch (error) {
    message.textContent = error.message;
  }
}

function scheduleRender(delay = 180) {
  clearTimeout(renderTimer);
  renderTimer = setTimeout(startRender, delay);
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  if (["starting", "running"].includes(currentPhase)) {
    currentPhase = "pausing";
    updateStartButton();
    enqueueCommand("/pause").then((status) => {
      currentPhase = status.phase;
      updateStartButton();
    }).catch((error) => {
      message.textContent = error.message;
    });
  } else if (currentPhase === "paused" && !renderDirty) {
    currentPhase = "resuming";
    updateStartButton();
    enqueueCommand("/resume").then((status) => {
      currentPhase = status.phase;
      updateStartButton();
    }).catch((error) => {
      message.textContent = error.message;
    });
  } else if (!["pausing", "resuming", "stopping"].includes(currentPhase)) {
    startRender();
  }
});

document.getElementById("stop").addEventListener("click", async () => {
  clearTimeout(renderTimer);
  ++requestNumber;
  currentPhase = "stopping";
  updateStartButton();
  try {
    const status = await enqueueCommand("/stop");
    currentPhase = status.phase;
    updateStartButton();
  } catch (error) {
    message.textContent = error.message;
  }
});
document.getElementById("preset").addEventListener("click", () => {
  clearTimeout(renderTimer);
  fullViewPreset();
  renderDirty = true;
  scheduleDisplay();
});

function perSecond(amount, seconds) {
  return seconds > 0 ? formatSamples(amount / seconds) : "0";
}

function formatMetric(m) {
  return `${format3(m.amount)}${m.unit === "%" ? "%" : ` ${m.unit}`} ${m.name}`;
}

// Metrics common to every sampler, then the sampler's primary ones.
function statusText(status) {
  const size = status.width ? `${status.width}x${status.height}` : "—";
  const primary = (status.sampler_metrics || []).filter((m) => m.primary);
  return [
    status.phase,
    size,
    `${formatSamples(status.orbits)} orbits`,
    `${perSecond(status.orbits, status.elapsed)} orbits/s`,
    `${perSecond(status.points, status.elapsed)} points/s`,
    ...primary.map(formatMetric),
  ].join(" · ");
}

// Pipeline timings and the remaining sampler metrics.
function debugText(status) {
  const average = (seconds, count) =>
    count ? `${format3(seconds * 1000 / count)}ms` : "—";
  const others = (status.sampler_metrics || []).filter((m) => !m.primary);
  return [
    `${status.sampler || "—"} sampler`,
    `${status.batches} batches`,
    `GPU ${perSecond(status.orbits, status.batch_seconds)} orbits/s`,
    `${status.orbits ? format3(100 * status.drawn / status.orbits) : 0}% drawn`,
    `${status.orbits ? format3(status.steps / status.orbits) : 0} steps/orbit`,
    ...others.map(formatMetric),
    `preview ${average(status.preview_seconds, status.preview_count)}`,
    `capture ${average(status.capture_seconds, status.capture_count)}`,
    `recolor ${average(status.recolor_seconds, status.recolor_count)}`,
  ].join(" · ");
}

function setDebug(enabled) {
  debugToggle.checked = enabled;
  debugLine.hidden = !enabled;
  try {
    localStorage.setItem("debug", enabled ? "1" : "0");
  } catch {
    // Storage may be unavailable; the toggle still works for this page.
  }
}

debugToggle.addEventListener("change", () => setDebug(debugToggle.checked));

async function poll() {
  try {
    const status = await getStatus();
    if (!displayInitialized && status.display) {
      if (!displayTouched) {
        for (const name of displayNames) {
          form.elements[name].value = status.display[name];
        }
        updateDisplayLabels();
      }
      displayInitialized = true;
    }
    activeRenderId = status.render_id;
    currentPhase = status.phase;
    updateStartButton();
    statusLine.textContent = statusText(status);
    debugLine.textContent = debugText(status);
    if (status.error) message.textContent = status.error;
    if (
      status.frame_render_id === activeRenderId &&
      status.frame_revision !== lastFrameRevision && !frameLoading
    ) {
      frameLoading = true;
      const renderId = activeRenderId;
      try {
        const pixels = await getFrame(status.frame_revision);
        if (pixels === null) return status;
        if (pixels.length !== status.width * status.height * 4) {
          throw new Error("Invalid frame size");
        }
        if (renderId === activeRenderId) {
          image.width = status.width;
          image.height = status.height;
          image.getContext("2d").putImageData(
            new ImageData(pixels, status.width, status.height),
            0,
            0,
          );
          lastFrameRevision = status.frame_revision;
          paintedRenderId = renderId;
          paintedDisplayRevision = status.frame_display_revision;
          message.textContent = `Showing render ${activeRenderId}`;
        }
      } catch (error) {
        message.textContent = "Preview: " + error.message;
      } finally {
        frameLoading = false;
      }
    }
    return status;
  } catch {
    statusLine.textContent = "Renderer disconnected";
  }
}

async function refreshUntil(done) {
  const token = ++refreshToken;
  for (let attempt = 0; attempt < 25 && token === refreshToken; ++attempt) {
    const status = await poll();
    if (!status || done(status)) return;
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

attachGeometry({
  form,
  viewport,
  validSettings,
  scheduleRender,
  cancelRender: () => clearTimeout(renderTimer),
  getActiveRenderId: () => activeRenderId,
});
fullViewPreset();
try {
  setDebug(localStorage.getItem("debug") === "1");
} catch {
  setDebug(false);
}
poll();
setInterval(poll, 1000);
