# Metal rendering

This directory holds `buddha-metal`, an experimental Apple-silicon GPU
renderer, and the benchmarks that led to it. Everything here is built only on
macOS.

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
loaded exactly as `buddha++` loads it (create one with the `exclusion` tool);
`--threads` sets only the checkpoint compression threads.

How it differs from `buddha++`:

- **Sampler.** Starting points are sampled uniformly over `[-2, 2]²` (naive
  Buddhabrot), not with Metropolis chains, so the image looks different
  (short orbits weigh more). Like any naive sampler, zoomed-in views waste
  most samples on orbits that never reach the window, which is what Metropolis
  was added for. Checkpoints validate geometry and iteration ranges, not the
  sampler: do not mix `buddha++` and `buddha-metal` checkpoints.
- **Precision.** Apple GPUs have no hardware double precision, so orbits use
  `float`. At full view this is visually identical to double (see the
  benchmark below); deep zooms are untested.
- **Formula.** Only `z = z * z + c`.
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
the sampling lane. The build inlines the header into `metal/render.metal` and
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
  checks that the three `mandelbrot_base::evaluate` overloads agree.
- `metal-consistency` runs the lane in float on the CPU and the Metal kernel
  on the GPU, and requires identical counters and histograms. It reports
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

## Orbit-only benchmark

This is deliberately not a renderer.  It compares the CPU and Metal versions
of the same naive, single-precision `z = z * z + c` orbit loop.  Neither path
uses a histogram, checkpoint, image buffer, or image encoder.

The GPU writes one pair of counters per threadgroup, rather than one result per
orbit.  That tiny buffer prevents the optimizer from removing the orbit loop
without measuring global histogram atomic contention.

Configure and build with:

```sh
cmake -S . -B build-metal -DCMAKE_BUILD_TYPE=Release
cmake --build build-metal --target metal-orbit-benchmark --parallel
./build-metal/metal-orbit-benchmark --orbits 1000000 --iterations 1024
```

The executable accepts `--orbits`, `--iterations`, `--seed`, and `--threads`.
The CPU and GPU totals are diagnostic checks only: small floating-point
differences can alter escape trajectories, so throughput is the comparison.

Each run also reports memory in MiB as **before / sampled peak / after**.
Resident is process memory held in physical RAM; footprint is macOS's physical
memory accounting for the process. The Metal line additionally shows
`MTLDevice.currentAllocatedSize`, the size of Metal resources allocated by
this process. These values overlap on unified-memory Macs and must not be
added together. The Metal interval includes shader compilation, buffer
allocation, warm-up, and the measured dispatch. CPU and Metal run in the same
process, so the Metal baseline includes memory retained by the CPU run.

The process measurements are sampled every 5 ms plus at interval boundaries;
short-lived peaks may be missed. Sampling adds a small amount of CPU work, so
throughput is best compared across repeated runs with the same settings. The
benchmark needs a Metal-capable device at runtime; Xcode's Metal tools are not
needed because the kernel is compiled from source by the Metal API.

## Naive render benchmark

`metal-render-benchmark` measures a more realistic naive (uniform sampling, no
Metropolis) Buddhabrot workload: the renderer's exclusion map, its periodicity
check, and RGB histogram writes into a full-size histogram. Every
implementation evaluates the same counter-hashed sample points:

- **CPU double (renderer code)** uses the renderer's own code: `mandelbrot`
  exclusion lookup, the double-precision loop into a stored orbit, and
  `buddha_generator::drawPoint` into the shared atomic histogram. It is built
  with the project flags, like `buddha++`.
- **CPU float (shared lane)** runs the shared lane on CPU threads, for a
  like-for-like hardware comparison.
- **Metal float** is the kernel `buddha-metal` renders with.

```sh
cmake --build build-metal --target metal-render-benchmark --parallel
./build-metal/metal-render-benchmark --samples 1000000000 --exclusion-map exclusion.map
```

Unlike the renderers, the benchmark generates a missing map and saves it;
`--exclusion-map none` disables it. Defaults match the renderer's colour
ranges (red 512–8192, green 128–2048, blue 32–512) at 8192×8192, scale 2048.
Histograms are compared per bin and on 16×16 pixel blocks; per-bin differences
are expected because long orbits are chaotic in either precision.

### Results

Apple M5 Pro, 15 CPU threads, 1e9 samples, 4096 exclusion map:

| Path | Time | Samples/s | Histogram increments/s |
| --- | ---: | ---: | ---: |
| CPU double (renderer code) | 4.72 s | 212 M | 340 M |
| CPU float (shared lane) | 4.83 s | 207 M | 333 M |
| Metal float | **1.12 s** | 892 M | 1,434 M |

The CPU float and Metal histograms are bit-identical. The double and float
images agree on 16×16 blocks (correlation 0.99993, under 1% relative L1), so
float precision does not visibly change a naive render at this scale;
individual bins differ because long orbits are chaotic in either precision.

The measurements below were taken while developing the kernel, before exact
rounding (which costs the GPU about 3%) and with an earlier one-sample-per-
thread kernel that is no longer built:

| Path | Time |
| --- | ---: |
| CPU double (renderer code) | 4.60 s |
| Metal float, one sample per thread | 2.49 s |
| Metal float, persistent threads | 1.07 s |

#### Why one sample per thread is slow

A GPU core executes a SIMD group of 32 threads with one instruction stream.
Divergent lanes are masked, and a loop runs until the group's slowest lane
finishes. With one sample per thread, every group costs as much as its longest
orbit. The exclusion map and periodicity check remove the long, uniform
interior orbits that kept lanes busy in the orbit-only benchmark. What remains
is mostly orbits that escape within a few steps, plus rare long ones that hold
their whole group.

Removing histogram writes (colour ranges that draw nothing, same orbit work)
takes the CPU from 4.63 s to 2.78 s, but the one-sample kernel only from
2.50 s to 2.24 s. Writes are about 40% of CPU time and 10% of GPU time, so
divergence, not atomics, limits that kernel.

#### Persistent threads

With persistent lanes (see [Shared kernel](#shared-kernel)), divergence is
limited to the short load and draw branches. The thread count matters
(`--gpu-threads`, `--batch`; 1e9 samples):

| Threads | 2^26 samples/dispatch | 2^28 samples/dispatch |
| ---: | ---: | ---: |
| 8,192 | 1.39 s | 1.27 s |
| 32,768 | 1.23 s | 1.07 s |
| 262,144 | 1.64 s | — |
| 4,194,304 | 3.33 s | — |

It is flat between about 24k and 64k threads with large batches, so the
defaults are 32,768 threads and 2^28 samples per dispatch. Large batches
shrink each dispatch's tail, when lanes wait for the last long orbits. Using
fewer threads than the GPU could hold also helped, probably from longer
refill runs per thread and less histogram contention. The kernel still spends
extra steps re-iterating escaping orbits (about half as many again as pass 1)
because orbits are not stored on the GPU.
