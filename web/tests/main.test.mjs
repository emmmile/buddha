import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { setImmediate } from "node:timers/promises";
import { test } from "node:test";
import { URL } from "node:url";
import { loadSource } from "./load-source.mjs";

// Replace only the DOM, GPU and encoder boundaries. Run main.ts with the real form/settings
// modules so these checks exercise user actions, render restarts and metadata together.
function setup({ available = true, hash = "" } = {}) {
  class Element {
    listeners = new Map();
    style = { visibility: "" };
    disabled = false;
    hidden = false;
    textContent = "";
    children = [];
    parentElement = null;
    classList = { toggle() {}, add() {}, remove() {} };
    addEventListener(name, callback) {
      this.listeners.set(name, callback);
    }
    dispatch(name, event = {}) {
      this.listeners.get(name)?.(event);
    }
    click() {
      if (!this.disabled) this.dispatch("click");
    }
    append(...children) {
      this.children.push(...children);
    }
    setAttribute() {}
  }
  class Input extends Element {
    value = "";
    error = "";
    get valueAsNumber() {
      return this.value === "" ? NaN : Number(this.value);
    }
    setCustomValidity(error) {
      this.error = error;
    }
  }
  class Select extends Input {}
  class Details extends Element {
    open = false;
  }
  const html = readFileSync(new URL("../index.html", import.meta.url), "utf8");
  const fields = new Map(
    [...html.matchAll(/<(input|select)[^>]*name="([^"]+)"/g)].map(([, tag, name]) => {
      const input = tag === "select" ? new Select() : new Input();
      input.name = name;
      return [name, input];
    })
  );
  const form = new Element();
  form.elements = { namedItem: (name) => fields.get(name) };
  form.querySelectorAll = () => [...fields.values()].filter((input) => input.error);
  form.reportValidity = () => false;
  const elements = new Map(
    [...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [id, new Element()])
  );
  elements.set("controls", form);
  elements.get("pause").disabled = elements.get("export").disabled = true;
  const canvas = elements.get("image");
  canvas.getContext = () => ({ configure() {} });
  const globals = new Element();
  const timers = new Map(),
    frames = new Map();
  let id = 0,
    resized,
    viewChanged;
  const samplers = [],
    devices = [],
    exports = [];
  class Sampler {
    running = false;
    mirrorAxis = 0;
    metrics = { seeds: 0, proposals: 0, increments: 0 };
    starts = [];
    constructor() {
      samplers.push(this);
    }
    setHistogram() {}
    start(settings, running = true) {
      this.starts.push(settings);
      this.running = running;
    }
    pause() {
      this.running = false;
    }
    resume() {
      this.running = true;
    }
    elapsed() {
      return 0;
    }
  }
  class Display {
    setHistogram() {}
    draw() {}
    async capture() {
      return { data: new Uint8Array(4) };
    }
  }
  class Worker {
    postMessage(request) {
      exports.push(JSON.parse(request.text["buddha-explorer"]));
      this.onmessage({ data: { type: "image/png" } });
    }
    terminate() {}
  }
  class TestUrl extends URL {
    static createObjectURL() {
      return "blob:export";
    }
    static revokeObjectURL() {}
  }
  const navigator = {};
  const adapter = {
    features: new Set(),
    info: {},
    limits: {},
    async requestDevice() {
      let lose;
      const device = {
        limits: {
          maxStorageBufferBindingSize: 1e8,
          maxBufferSize: 1e8,
          maxComputeWorkgroupsPerDimension: 65535,
          maxTextureDimension2D: 8192,
        },
        lost: new Promise((resolve) => {
          lose = resolve;
        }),
        listeners: new Map(),
        addEventListener(name, callback) {
          this.listeners.set(name, callback);
        },
        createShaderModule: () => ({ getCompilationInfo: async () => ({ messages: [] }) }),
        createBuffer: () => ({}),
        destroy() {
          lose({ message: "Destroyed", reason: "destroyed" });
        },
        lose() {
          lose({ message: "Simulated loss", reason: "unknown" });
        },
      };
      devices.push(device);
      return device;
    },
  };
  const enableGpu = () => {
    navigator.gpu = {
      requestAdapter: async () => adapter,
      getPreferredCanvasFormat: () => "rgba8unorm",
    };
  };
  if (available) enableGpu();
  const location = { hash };
  loadSource(
    "main.ts",
    {
      HTMLInputElement: Input,
      HTMLSelectElement: Select,
      HTMLDetailsElement: Details,
      navigator,
      performance,
      location,
      window: {},
      Worker,
      URL: TestUrl,
      document: {
        getElementById: (id) => elements.get(id),
        createElement: () => new Element(),
        body: new Element(),
      },
      history: {
        state: null,
        replaceState(_state, _title, hash) {
          location.hash = hash;
        },
      },
      GPUBufferUsage: { STORAGE: 1, COPY_DST: 2 },
      addEventListener: (name, callback) => globals.addEventListener(name, callback),
      requestAnimationFrame: (callback) => {
        frames.set(++id, callback);
        return id;
      },
      cancelAnimationFrame: (id) => frames.delete(id),
      setTimeout: (callback, delay) => {
        timers.set(++id, { callback, delay });
        return id;
      },
      clearTimeout: (id) => timers.delete(id),
    },
    {
      "./style.css": {},
      "./shaders/display.wgsl?raw": {},
      "./shaders/metropolis.wgsl?raw": {},
      "./sampler": { Sampler },
      "./display": { Display },
      "./exclusion": { loadExclusionMap: async () => ({}) },
      "./geometry": {
        fitPixels: (size) => size,
        sameSize: (a, b) => a.width === b.width && a.height === b.height,
        attachGeometry(options) {
          viewChanged = options.viewChanged;
        },
        observeDevicePixels(_canvas, changed) {
          resized = changed;
          changed({ width: 800, height: 600 });
        },
      },
    }
  );
  const frame = () => {
    const callbacks = [...frames.values()];
    frames.clear();
    callbacks.forEach((callback) => callback(performance.now()));
  };
  return {
    elements,
    fields,
    samplers,
    devices,
    exports,
    location,
    frame,
    enableGpu,
    resize(size) {
      resized(size);
    },
    timers(delay) {
      for (const [id, timer] of timers) {
        if (timer.delay === delay) {
          timers.delete(id);
          timer.callback();
        }
      }
    },
    edit(name, value, event = "change") {
      fields.get(name).value = String(value);
      form.dispatch(event, { target: fields.get(name) });
    },
    navigate() {
      viewChanged();
    },
    changeHash(hash) {
      location.hash = hash;
      globals.dispatch("hashchange");
    },
  };
}

test("GPU actions stay disabled during startup; unsupported browsers can retry successfully", async () => {
  const app = setup({ available: false });
  assert.equal(app.elements.get("pause").disabled, true);
  assert.equal(app.elements.get("export").disabled, true);
  await setImmediate();
  assert.match(app.elements.get("message").textContent, /WebGPU is not available/);
  assert.equal(app.elements.get("retry").hidden, false);
  app.enableGpu();
  app.elements.get("retry").click();
  assert.equal(app.elements.get("retry").hidden, true);
  assert.equal(app.elements.get("pause").disabled, true);
  await setImmediate();
  app.frame();
  assert.equal(app.elements.get("pause").disabled, false);
  assert.equal(app.elements.get("export").disabled, false);
  assert.equal(app.samplers.length, 1);
});

test("Pause persists through navigation, settings, resize, hash changes and both resets", async () => {
  const app = setup();
  await setImmediate();
  app.frame();
  const sampler = app.samplers[0];
  app.elements.get("pause").click();
  app.edit("gamma", 0.5, "input");
  app.edit("radius", 2);
  app.frame();
  assert.equal(sampler.running, false);
  app.fields.get("cre").value = "1.4";
  app.navigate();
  app.frame();
  assert.equal(sampler.running, false);
  app.resize({ width: 900, height: 700 });
  app.timers(200);
  app.frame();
  assert.equal(sampler.running, false);
  app.changeHash("#re=0.25&im=0.75&span=0.1&radius=2&gamma=0.5");
  app.frame();
  assert.equal(sampler.running, false);
  app.elements.get("reset-view").click();
  app.frame();
  assert.equal(sampler.starts.at(-1).cre, 0);
  assert.equal(sampler.starts.at(-1).radius, 2);
  assert.equal(app.fields.get("gamma").value, "0.5");
  assert.equal(sampler.running, false);
  app.elements.get("reset-all").click();
  app.frame();
  assert.equal(sampler.starts.at(-1).radius, 1);
  assert.equal(app.fields.get("gamma").value, "0.3");
  assert.equal(sampler.running, false);
  assert.equal(app.elements.get("pause").textContent, "Resume");
  app.elements.get("pause").click();
  assert.equal(sampler.running, true);
});

test("export uses the rendered settings while invalid or uncommitted form drafts exist", async () => {
  const app = setup({ hash: "#re=1.4&span=0.02" });
  await setImmediate();
  app.frame();
  app.edit("cre", 9, "input");
  app.edit("span", "", "change");
  app.edit("gamma", 0.5, "input");
  app.elements.get("export").click();
  await setImmediate();
  assert.equal(app.exports[0].cre, 1.4);
  assert.equal(app.exports[0].span, 0.02);
  assert.equal(app.exports[0].display.gamma, 0.5);
  app.resize({ width: 900, height: 700 });
  app.timers(200);
  app.frame();
  assert.equal(app.samplers[0].starts.at(-1).cre, 1.4);
  assert.equal(app.samplers[0].starts.at(-1).scale, 700 / 0.02);
});

test("device loss and sampler failures stop rendering and permit a fresh GPU retry", async () => {
  const app = setup();
  await setImmediate();
  app.frame();
  app.elements.get("pause").click();
  app.devices[0].lose();
  await setImmediate();
  assert.equal(app.samplers[0].running, false);
  assert.equal(app.elements.get("pause").disabled, true);
  assert.equal(app.elements.get("export").disabled, true);
  assert.equal(app.elements.get("retry").hidden, false);
  assert.match(app.elements.get("message").textContent, /Simulated loss/);
  app.elements.get("retry").click();
  await setImmediate();
  app.frame();
  assert.equal(app.samplers.length, 2);
  assert.equal(app.samplers[1].running, false);
  app.samplers[1].onError(new Error("Readback failed"));
  assert.equal(app.elements.get("retry").hidden, false);
  assert.equal(app.elements.get("message").textContent, "Readback failed");
});
