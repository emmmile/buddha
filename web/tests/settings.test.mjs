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
