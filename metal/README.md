# Metal rendering

This directory holds `buddha-metal`, an experimental Apple-silicon GPU
renderer. It is built only on macOS.

## buddha-metal

`buddha-metal` is `buddha++` with a GPU generator. Options, exclusion map,
checkpoints and TIFF output all go through the same `buddha` object, so the
command line and files are the same:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target buddha-metal --parallel
./build/buddha-metal --width 8192 --height 8192 --scale 2048 --out render
./build/buddha-metal --width 8192 --height 8192 --scale 2048 --load render.zst   # resume
```

Press `Ctrl-C` (or send `SIGTERM`) to stop and save. The exclusion map is
loaded exactly as `buddha++` loads it (by default the committed
`data/exclusion.map`);
`--threads` sets only the checkpoint compression threads.

How it differs from `buddha++`:

- **Sampler.** Starting points are sampled uniformly over `[-2, 2]²` (naive
  Buddhabrot), not with Metropolis chains; `buddha++ --sampler naive` runs the
  same sampler on the CPU. The image looks different from Metropolis
  (short orbits weigh more). Like any naive sampler, zoomed-in views waste
  most samples on orbits that never reach the window, which is what Metropolis
  was added for. Checkpoints record the sampler, so `buddha-metal` refuses
  Metropolis checkpoints; `buddha++ --sampler naive` can continue its
  checkpoints and the other way round.
- **Precision.** Apple GPUs have no hardware double precision, so orbits use
  `float`. At full view this is visually identical to double (see the
  benchmark below); deep zooms are untested.
- **Size.** Histogram indices are 32-bit: up to about 53,500² for windows on
  the real axis (mirrored histogram) or 37,800² off-axis.

The GPU renders straight into the renderer's histogram. `buddha::raw` is
allocated in whole, page-aligned pages (`core/page_allocator.h`), which lets
Metal wrap that memory without a copy (`newBufferWithBytesNoCopy`); on Apple
silicon CPU and GPU share it. Loading a checkpoint, rendering and saving all
use the one histogram, so memory use matches `buddha++` (706 MB peak versus
655 MB for `buddha++` at 8192², including TIFF output and the Metal runtime).

## Shared kernel

`core/buddha_kernel.h` compiles as both C++ and Metal Shading Language. It
holds the rendering rules, used by `buddha++` too (`mandelbrot_base`,
`mandelbrot::excluded`, `buddha_generator::drawPoint`):

- the escape test and the periodicity schedule;
- the exclusion-map cell and lookup;
- pixel mapping, mirroring and the odd-height centre-row weight;
- colour-channel iteration ranges;

and the naive sampler: kernel parameters, the counter-based random stream, and
the sampling lane. `buddha_generator::naive` (`buddha++ --sampler naive`) runs
the same lane in float on the CPU, with the same parameters
(`settings::kernel_parameters`), so both renderers run one naive sampler. The build inlines the header into `metal/render.metal` and
embeds the result, which Metal compiles at run time (Xcode is not needed); the
combined source is also written to `build/generated/buddha.metal`.

The lane is a state machine that advances one orbit step per call. GPUs run
32-thread SIMD groups in lockstep, so a kernel that gives each thread one
sample makes every group wait for its longest orbit. Each GPU thread instead
runs one lane over many samples and loads its next starting point as soon as
an orbit ends. Pass 1 applies the escape and periodicity rules; orbits are not
stored on the GPU, so pass 2 re-iterates escaping orbits to draw steps
`low..orbitMax`, as the CPU renderer draws a stored orbit. Each dispatch uses
a fresh random key.

Shared arithmetic rounds every operation separately (`BUDDHA_EXACT`, and
`#pragma clang fp contract(off)` in `render.metal`), whatever the including
code's flags. The lane's two passes then follow the same orbit, and the CPU
and GPU compute bit-identical results. The CPU renderer keeps its own
`std::complex` recurrence and stored orbit.

Two tests catch drift, and CI runs both (`.github/workflows/ci.yml`):

- `kernel-consistency` runs identical samples through `buddha++`'s own loop
  and `drawPoint` and through the shared lane in double precision, and
  requires identical classifications, step counts and histograms. It also
  checks that the three `mandelbrot_base::evaluate` overloads agree and that
  `buddha_generator::naive` matches the lane.
- `metal-consistency` runs the lane in float on the CPU, `buddha_generator::naive`
  and the Metal kernel on the GPU, and requires identical counters and
  histograms. It reports
  itself skipped without a Metal device; CI then still compiles the Metal
  source ahead of time.

Refactoring `buddha++` onto the shared rules does not change what it
computes: built with strict IEEE arithmetic, the old and new code give
identical Metropolis chains, naive orbits, exclusion lookups and maps. With
the project's `-ffast-math`, the compiler may round differently than before,
so a chain can diverge from an older binary's after a last-bit difference.
Renders are randomly seeded, so no two runs matched anyway. On an M5 Pro the
single-thread Metropolis loop got about 30% faster (77 versus 59 M steps/s),
probably because the periodicity checkpoint now stays in registers.

## Benchmarks

The benchmarks behind these numbers are not built any more. They are kept in the
repository history at commit [`37787f4`](https://github.com/emmmile/buddha/blob/37787f438951b63dd665d87a650ee1265d57cdbc):

- [`metal/orbit_benchmark.mm`](https://github.com/emmmile/buddha/blob/37787f438951b63dd665d87a650ee1265d57cdbc/metal/orbit_benchmark.mm): the bare
  `z = z * z + c` loop on CPU and GPU, with no histogram, exclusion map or
  periodicity check, plus memory sampling.
- [`metal/render_benchmark.mm`](https://github.com/emmmile/buddha/blob/37787f438951b63dd665d87a650ee1265d57cdbc/metal/render_benchmark.mm): naive
  sampling with the exclusion map, periodicity check and RGB histogram. It
  runs identical samples through the CPU renderer's code (double), the shared
  lane on CPU threads (float) and the Metal kernel, and compares the
  histograms.

To reproduce, build that commit in a separate checkout:

```sh
git worktree add ../buddha-benchmarks 37787f4
cmake -S ../buddha-benchmarks -B ../buddha-benchmarks/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../buddha-benchmarks/build --target metal-orbit-benchmark metal-render-benchmark
../buddha-benchmarks/build/metal-render-benchmark --samples 1000000000 --exclusion-map exclusion.map
```

Results on an Apple M5 Pro (16 GPU cores, 15 CPU threads).

**Orbit loop only**, 10M uniform samples: Metal is about 6× faster than the CPU
(31.5–32.2 versus 5.1–5.3 G iterations/s, for 1,024 and 8,192 iterations).

**Naive render**, 8192² at scale 2048, 1e9 samples, 4096 exclusion map:

| Path | Time | Samples/s | Histogram increments/s |
| --- | ---: | ---: | ---: |
| CPU double (renderer code) | 4.72 s | 212 M | 340 M |
| CPU float (shared lane) | 4.83 s | 207 M | 333 M |
| Metal float | **1.12 s** | 892 M | 1,434 M |

The CPU float and Metal histograms are bit-identical. The double and float
images agree on 16×16 blocks (correlation 0.99993, under 1% relative L1), so
float precision does not visibly change a naive render at this scale;
individual bins differ because long orbits are chaotic in either precision.

The render gains less than the bare loop because the exclusion map and
periodicity check remove the long, uniform interior orbits that keep GPU lanes
busy. A GPU core executes a SIMD group of 32 threads with one instruction
stream, and a loop runs until the group's slowest lane finishes. An earlier
kernel with one sample per thread took 2.49 s: removing its histogram writes
saved only 10% (versus 40% on the CPU), so divergence, not atomics, limited
it. Persistent lanes (see [Shared kernel](#shared-kernel)) brought that to
1.07 s; exact rounding later cost the GPU about 3%.

The thread count matters (1e9 samples):

| Threads | 2^26 samples/dispatch | 2^28 samples/dispatch |
| ---: | ---: | ---: |
| 8,192 | 1.39 s | 1.27 s |
| 32,768 | 1.23 s | 1.07 s |
| 262,144 | 1.64 s | — |
| 4,194,304 | 3.33 s | — |

It is flat between about 24k and 64k threads (1,500–4,000 per core) with large
batches. `buddha-metal` runs 2,048 threads per core, with the core count read
from the IOKit registry (Metal does not expose it), falling back to 32,768
threads; dispatches are 2^28 samples. Large batches shrink each dispatch's
tail, when lanes wait for the last long orbits. Other GPUs are untested. The
kernel spends about half as many steps again re-iterating escaping orbits,
because orbits are not stored on the GPU.

## Metropolis chains


The shared kernel also runs Metropolis–Hastings chains on the GPU (`chain` in
`core/buddha_kernel.h`, the `metropolis` kernel in `render.metal`).
`buddha-metal` does not use them yet. The browser explorer runs a WGSL port in
`web/src/shaders/metropolis.wgsl`. A chain
mutates its current starting point by a small random step and accepts the
proposal with probability `min(1, f'/f)`, where `f = L^a · C^b`, `L` is the
orbit's escape iteration and `C` the number of orbit points in the window.
Every valid proposal is drawn once, accepted or not, with no reweighting: the
image is biased toward long orbits that cross the window, which is the look of
the original CPU sampler (`buddha_generator::metropolis`). A chain runs
`max(256 C, 2 L)` proposals, times a factor, from a seed found by a random walk
from the origin or uniformly, then starts again.

The earlier prototype ran one whole chain per GPU thread and was about 8.5×
slower than the CPU, because chain lengths vary by over 25× and threads waited
for the longest one. Here each thread is a lane that performs one orbit step
per call, like the naive lane, and keeps its chain in a buffer between
dispatches; a dispatch gives every lane the same number of steps.

Accepting a proposal depends on logarithms, and mutations on sines and cosines.
The kernel computes them with the single-precision Cephes polynomials, using
only `+`, `-`, `*` and integer operations, so the CPU and GPU make identical
decisions: `metal-consistency` runs the same chains on both and requires
identical counters and histograms.
