import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import { performance } from "node:perf_hooks";
import { setImmediate } from "node:timers/promises";
import { test } from "node:test";
import { loadSource } from "./load-source.mjs";

function setup() {
  const readbacks = [];
  let submissions = 0;
  const device = {
    createBindGroupLayout: () => ({}),
    createPipelineLayout: () => ({}),
    createComputePipeline: () => ({}),
    createBindGroup: () => ({}),
    createBuffer({ size, usage }) {
      let resolve, reject;
      const promise = new Promise((ok, fail) => {
        resolve = ok;
        reject = fail;
      });
      // Only mapped readbacks need a pending promise.
      if (usage & 16) readbacks.push({ resolve, reject });
      else resolve();
      return {
        mapAsync: () => promise,
        getMappedRange: () => new ArrayBuffer(size),
        unmap() {},
        destroy() {},
      };
    },
    createCommandEncoder: () => ({
      clearBuffer() {},
      copyBufferToBuffer() {},
      beginComputePass: () => ({
        setPipeline() {},
        setBindGroup() {},
        dispatchWorkgroups() {},
        end() {},
      }),
      finish: () => ({}),
    }),
    queue: {
      writeBuffer() {},
      submit() {
        ++submissions;
      },
    },
  };
  const { Sampler } = loadSource("sampler.ts", {
    crypto: webcrypto,
    performance,
    GPUShaderStage: { COMPUTE: 1 },
    GPUMapMode: { READ: 1 },
    GPUBufferUsage: { UNIFORM: 1, COPY_DST: 2, STORAGE: 4, COPY_SRC: 8, MAP_READ: 16 },
  });
  const sampler = new Sampler(device, {}, { cells: {}, boundary: {} }, false);
  sampler.setHistogram({});
  const { defaults, samplerSettings } = loadSource("settings.ts");
  return {
    sampler,
    readbacks,
    settings: samplerSettings(defaults, 800, 800),
    submissions: () => submissions,
  };
}

test("starting a paused histogram submits no sampling batches until resume", async () => {
  const { sampler, settings, submissions, readbacks } = setup();
  sampler.start(settings, false);
  assert.equal(sampler.running, false);
  assert.equal(sampler.elapsed(), 0);
  assert.equal(submissions(), 1);
  sampler.resume();
  assert.equal(sampler.running, true);
  assert.equal(submissions(), 3);
  sampler.pause();
  for (const pending of readbacks) pending.resolve();
  await setImmediate();
  assert.equal(submissions(), 3);
  sampler.start({ ...settings, cre: 1 }, false);
  assert.equal(sampler.running, false);
  assert.equal(submissions(), 4);
});

test("a rejected GPU readback stops sampling and reports the error", async () => {
  const { sampler, settings, readbacks, submissions } = setup();
  const failure = new Error("Device lost during readback");
  const errors = [];
  sampler.onError = (error) => errors.push(error);
  sampler.start(settings);
  readbacks[0].reject(failure);
  readbacks[1].resolve();
  await setImmediate();
  assert.equal(sampler.running, false);
  assert.equal(errors[0], failure);
  assert.equal(submissions(), 3);
});
