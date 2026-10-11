import assert from "node:assert/strict";
import { test } from "node:test";
import { loadSource } from "./load-source.mjs";

const { defaults, settingsHash, settingsFromHash, samplerSettings } = loadSource("settings.ts");

test("existing link names round-trip settings and preserve shader option indices", () => {
  const settings = {
    ...defaults,
    cre: -0.123456789012345,
    cim: 0.75,
    span: 0.0148875,
    sampler: "naive",
    seeding: "walk",
    lowr: 1024,
    exposure: -0.45,
    gamma: 0.42,
  };
  const hash = settingsHash(settings);
  assert.match(hash, /re=-0.123456789012345&im=0.75/);
  assert.deepEqual({ ...settingsFromHash(hash) }, settings);
  const gpu = samplerSettings(settings, 1200, 800);
  assert.equal(gpu.scale, 800 / settings.span);
  assert.equal(gpu.sampler, 1);
  assert.equal(gpu.seeding, 0);
  assert.equal(samplerSettings(defaults, 800, 800).seeding, 2);
});

test("URL validation rejects invalid dormant tuning, channel ranges and unknown options", () => {
  const parsed = settingsFromHash(
    "#sampler=naive&radius=-5&seeding=unknown&lowr=9000&highr=500&gamma=0&span=Infinity&highb=2.5"
  );
  for (const name of ["radius", "seeding", "lowr", "highr", "gamma", "span", "highb"]) {
    assert.equal(parsed[name], defaults[name], name);
  }
  assert.equal(parsed.sampler, "naive");
});

function setup() {
  class Input {
    value = "";
  }
  const fields = new Map(["cre", "cim", "span"].map((name) => [name, new Input()]));
  fields.get("cre").value = fields.get("cim").value = "0";
  fields.get("span").value = "4";
  const listeners = new Map();
  const { attachGeometry } = loadSource("geometry.ts", { HTMLInputElement: Input });
  const viewport = {
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 1200, height: 800 }),
    addEventListener: (name, callback) => listeners.set(name, callback),
  };
  attachGeometry({
    form: { elements: { namedItem: (name) => fields.get(name) } },
    viewport,
    validSettings: () => true,
    viewChanged() {},
  });
  return { fields, wheel: listeners.get("wheel") };
}

test("wheel zoom doubles the former step and preserves the pointer anchor", () => {
  const { fields, wheel } = setup();
  wheel({ clientX: 900, clientY: 200, deltaY: -100, deltaMode: 0, preventDefault() {} });
  const span = Number(fields.get("span").value);
  assert.ok(Math.abs(span - 4 * Math.exp(-0.2)) < 1e-13);
  assert.ok(Math.abs(Number(fields.get("cre").value) - (200 * span) / 800 + 1) < 1e-13);
  assert.ok(Math.abs(Number(fields.get("cim").value) + (300 * span) / 800 - 1.5) < 1e-13);
  wheel({ clientX: 900, clientY: 200, deltaY: 100, deltaMode: 0, preventDefault() {} });
  assert.ok(Math.abs(Number(fields.get("span").value) - 4) < 1e-13);
});

const { previewTransform, previousOpacity } = loadSource("transition.ts");
const view = { width: 1200, height: 800, cre: 0, cim: 0, span: 4 };

test("a zoom maps the pointer's complex location to the same pixel of the previous frame", () => {
  // Anchor at pixel (900, 200), with Re downward and Im rightward. Halving the span shifts
  // the centre to Re=-0.5, Im=0.75, leaving the complex anchor under that pointer.
  const next = { ...view, cre: -0.5, cim: 0.75, span: 2 };
  const transform = previewTransform(view, next, 1);
  assert.equal(0.75 * transform.scale[0] + transform.offset[0], 0.75);
  assert.equal(0.25 * transform.scale[1] + transform.offset[1], 0.25);
});

test("pan and repeated zoom transforms retain rotated coordinates and handle rectangular views", () => {
  const pan = previewTransform(view, { ...view, cre: 1, cim: -0.5 }, 0.5);
  assert.deepEqual([...pan.scale], [1, 1]);
  assert.equal(pan.offset[0], -0.5 / 6);
  assert.equal(pan.offset[1], 0.25);
  const first = { ...view, span: 2 };
  const repeated = previewTransform(first, { ...view, span: 1 }, 1);
  assert.deepEqual([...repeated.scale], [0.5, 0.5]);
  assert.deepEqual([...repeated.offset], [0.25, 0.25]);
});

test("placeholder opacity follows histogram density, holds empty renders and ends at the threshold", () => {
  assert.equal(previousOpacity(0, 100), 1);
  assert.equal(previousOpacity(100, 100), 1);
  assert.equal(previousOpacity(450, 100), 0.5);
  assert.equal(previousOpacity(800, 100), 0);
  assert.equal(previousOpacity(1000, 100), 0);
  assert.equal(previousOpacity(900, 200), previousOpacity(450, 100));
});
