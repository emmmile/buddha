import assert from "node:assert/strict";
import { test } from "node:test";
import { loadSource } from "./load-source.mjs";

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
