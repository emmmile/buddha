// Metropolis chains on the GPU (metropolis.wgsl): one chain per lane, two dispatches in flight,
// each resized toward TARGET_MS of GPU time. The counters are those of the Metal sampler, so the
// metrics compare directly with buddha-metal's.
const THREADS = 32768;
const TARGET_MS = 25;
const MIN_STEPS = 16;
const MAX_STEPS = 1 << 14; // orbit steps per lane per dispatch, as the Metal browser sampler ran
const IN_FLIGHT = 2;

// data/exclusion.map on the GPU (see exclusion.ts).
export interface ExclusionMap {
  size: number;
  cells: GPUBuffer;
}
const COUNTERS = [
  "seeds",
  "proposals",
  "accepted",
  "chains",
  "escaped",
  "increments",
  "iterations",
  "redraw",
] as const;

export interface SamplerSettings {
  width: number;
  height: number;
  cre: number;
  cim: number;
  scale: number; // pixels per complex unit
  lowr: number;
  highr: number;
  lowg: number;
  highg: number;
  lowb: number;
  highb: number;
  radius: number; // percent of the view's shorter side
  exponent_l: number;
  exponent_c: number;
  chain_scale: number;
  seeding: number; // 0: random walk, 1: uniform
}

// Sums of buddha_kernel::totals over the render, plus batch count and GPU time.
export type Metrics = Record<(typeof COUNTERS)[number], number> & {
  batches: number;
  gpuSeconds: number;
};

export class Sampler {
  onBatch: () => void = () => {};
  metrics!: Metrics;
  running = false;

  private device: GPUDevice;
  private layout: GPUBindGroupLayout;
  private beginPipeline: GPUComputePipeline;
  private metropolisPipeline: GPUComputePipeline;
  private params: GPUBuffer;
  private chains: GPUBuffer;
  private stats: GPUBuffer;
  private querySet?: GPUQuerySet;
  private resolve?: GPUBuffer;
  private readbacks: GPUBuffer[] = [];
  private histogram?: GPUBuffer;
  private group?: GPUBindGroup;
  private settings?: SamplerSettings;
  private steps = 256;
  private inFlight = 0;
  private generation = 0;
  private lastDone = 0;
  private elapsedBefore = 0;
  private resumedAt = 0;

  private exclusion: ExclusionMap;

  constructor(
    device: GPUDevice,
    module: GPUShaderModule,
    exclusion: ExclusionMap,
    timestamps: boolean
  ) {
    this.device = device;
    this.exclusion = exclusion;
    this.layout = device.createBindGroupLayout({
      entries: [0, 1, 2, 3, 4].map((binding) => ({
        binding,
        visibility: GPUShaderStage.COMPUTE,
        buffer: {
          type: binding === 0 ? "uniform" : binding === 4 ? "read-only-storage" : "storage",
        },
      })),
    });
    const layout = device.createPipelineLayout({ bindGroupLayouts: [this.layout] });
    this.beginPipeline = device.createComputePipeline({
      layout,
      compute: { module, entryPoint: "begin" },
    });
    this.metropolisPipeline = device.createComputePipeline({
      layout,
      compute: { module, entryPoint: "metropolis" },
    });
    this.params = device.createBuffer({
      size: 96,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.chains = device.createBuffer({ size: THREADS * 80, usage: GPUBufferUsage.STORAGE });
    this.stats = device.createBuffer({
      size: COUNTERS.length * 4,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
    });
    if (timestamps) {
      this.querySet = device.createQuerySet({ type: "timestamp", count: 2 });
      this.resolve = device.createBuffer({
        size: 16,
        usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
      });
    }
    this.resetMetrics();
  }

  get timestamps() {
    return Boolean(this.querySet);
  }

  get stepsPerLane() {
    return this.steps;
  }

  setHistogram(histogram: GPUBuffer) {
    this.histogram = histogram;
    this.group = this.device.createBindGroup({
      layout: this.layout,
      entries: [this.params, histogram, this.chains, this.stats, this.exclusion.cells].map(
        (buffer, binding) => ({
          binding,
          resource: { buffer },
        })
      ),
    });
  }

  private resetMetrics() {
    this.metrics = {
      ...(Object.fromEntries(COUNTERS.map((name) => [name, 0])) as Record<
        (typeof COUNTERS)[number],
        number
      >),
      batches: 0,
      gpuSeconds: 0,
    };
    this.elapsedBefore = 0;
    this.resumedAt = performance.now();
  }

  // Wall-clock seconds spent running, excluding pauses, as the native session counted them.
  elapsed() {
    return this.elapsedBefore + (this.running ? (performance.now() - this.resumedAt) / 1000 : 0);
  }

  // Starts a new histogram and new chains. Dispatches already queued finish first: the clear and
  // the chain restart follow them on the queue, and their results are discarded.
  start(settings: SamplerSettings) {
    if (!this.histogram) throw new Error("setHistogram must be called before start");
    this.settings = settings;
    ++this.generation;
    this.writeParams(crypto.getRandomValues(new Uint32Array(1))[0]);
    const encoder = this.device.createCommandEncoder();
    encoder.clearBuffer(this.histogram);
    this.pass(encoder, this.beginPipeline);
    this.device.queue.submit([encoder.finish()]);
    this.running = true;
    this.resetMetrics();
    this.pump();
  }

  pause() {
    if (!this.running) return;
    this.elapsedBefore = this.elapsed();
    this.running = false;
  }

  resume() {
    if (this.running || !this.settings) return;
    this.running = true;
    this.resumedAt = performance.now();
    this.pump();
  }

  private writeParams(key: number) {
    const s = this.settings!;
    const u = new Uint32Array(24);
    const f = new Float32Array(u.buffer);
    u.set([
      s.lowr,
      s.highr,
      s.lowg,
      s.highg,
      s.lowb,
      s.highb,
      Math.min(s.lowr, s.lowg, s.lowb),
      Math.max(s.highr, s.highg, s.highb),
      s.width,
      s.height,
      THREADS,
      this.steps,
      s.seeding,
      key,
      this.exclusion.size,
    ]);
    // The view is rotated as the TIFF output is: Re grows downward, Im rightward.
    f.set(
      [
        s.cre - s.height / 2 / s.scale,
        s.cim - s.width / 2 / s.scale,
        s.scale,
        ((s.radius / 100) * Math.min(s.width, s.height)) / s.scale,
        s.exponent_l,
        s.exponent_c,
        s.chain_scale,
      ],
      16
    );
    this.device.queue.writeBuffer(this.params, 0, u);
  }

  private pass(
    encoder: GPUCommandEncoder,
    pipeline: GPUComputePipeline,
    timestampWrites?: GPUComputePassTimestampWrites
  ) {
    const pass = encoder.beginComputePass(timestampWrites ? { timestampWrites } : {});
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, this.group!);
    pass.dispatchWorkgroups(THREADS / 64);
    pass.end();
  }

  private pump() {
    while (this.running && this.inFlight < IN_FLIGHT) this.dispatch();
  }

  private dispatch() {
    ++this.inFlight;
    const readback =
      this.readbacks.pop() ??
      this.device.createBuffer({
        size: 48,
        usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
      });
    this.writeParams(0);
    const encoder = this.device.createCommandEncoder();
    encoder.clearBuffer(this.stats);
    this.pass(
      encoder,
      this.metropolisPipeline,
      this.querySet && {
        querySet: this.querySet,
        beginningOfPassWriteIndex: 0,
        endOfPassWriteIndex: 1,
      }
    );
    encoder.copyBufferToBuffer(this.stats, 0, readback, 0, 32);
    if (this.querySet && this.resolve) {
      encoder.resolveQuerySet(this.querySet, 0, 2, this.resolve, 0);
      encoder.copyBufferToBuffer(this.resolve, 0, readback, 32, 16);
    }
    const generation = this.generation;
    const submitted = performance.now();
    this.device.queue.submit([encoder.finish()]);
    readback
      .mapAsync(GPUMapMode.READ)
      .then(() => {
        const data = readback.getMappedRange();
        const counters = new Uint32Array(data, 0, COUNTERS.length);
        const [begin, end] = new BigUint64Array(data, 32, 2);
        // Without timestamps, the time since the previous completion approximates the GPU time
        // of this dispatch while another one is queued behind it.
        const now = performance.now();
        const seconds =
          this.querySet && end > begin
            ? Number(end - begin) / 1e9
            : (now - Math.max(submitted, this.lastDone)) / 1000;
        this.lastDone = now;
        if (generation === this.generation) {
          COUNTERS.forEach((name, i) => (this.metrics[name] += counters[i]));
          ++this.metrics.batches;
          this.metrics.gpuSeconds += seconds;
        }
        readback.unmap();
        this.readbacks.push(readback);
        // Long dispatches risk the GPU watchdog and delay the display.
        if (seconds * 1000 < TARGET_MS * 0.7) {
          this.steps = Math.min(MAX_STEPS, Math.round(this.steps * 1.25));
        } else if (seconds * 1000 > TARGET_MS * 1.5) {
          this.steps = Math.max(MIN_STEPS, Math.round(this.steps * 0.7));
        }
        --this.inFlight;
        if (generation === this.generation) this.onBatch();
        this.pump();
      })
      .catch(() => --this.inFlight);
  }
}
