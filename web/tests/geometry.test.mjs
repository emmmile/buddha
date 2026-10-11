import assert from "node:assert/strict";
import { test } from "node:test";
import { loadSource } from "./load-source.mjs";

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
