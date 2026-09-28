import { enqueueCommand, getFrame, getStatus } from "./api.js";
import { attachGeometry, viewportPixelsFor } from "./geometry.js";

const form = document.getElementById("controls");
const startButton = document.getElementById("start");
const viewport = document.getElementById("viewport");
const image = document.getElementById("image");
const message = document.getElementById("message");
const statusLine = document.getElementById("status");
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
    const rate = status.elapsed > 0
      ? format3(status.samples / status.elapsed / 1e6)
      : "0";
    const batchRate = status.batch_seconds > 0
      ? format3(status.samples / status.batch_seconds / 1e6)
      : "0";
    const captureMs = status.capture_count
      ? ` · ${
        format3(status.capture_seconds * 1000 / status.capture_count)
      }ms avg capture`
      : "";
    const size = status.width ? `${status.width}x${status.height} pixels` : "—";
    const previewMs = status.preview_count
      ? format3(status.preview_seconds * 1000 / status.preview_count)
      : "0";
    const recolorMs = status.recolor_count
      ? ` · ${
        format3(status.recolor_seconds * 1000 / status.recolor_count)
      }ms avg recolor`
      : "";
    statusLine.textContent =
      `${status.phase} · ${size} · ${rate}M/s overall · ${batchRate}M/s GPU · ${previewMs}ms avg preview${captureMs}${recolorMs} · ${
        formatSamples(status.samples)
      } samples`;
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
poll();
setInterval(poll, 1000);
