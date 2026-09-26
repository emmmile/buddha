# Metal rendering

This directory holds `buddha-metal`, an experimental Apple-silicon GPU
renderer, and the benchmarks that led to it. Everything here is built only on
macOS.

## buddha-metal

`buddha-metal` renders a naive Buddhabrot on the GPU: starting points `c` are
sampled uniformly over `[-2, 2]²`, points inside the exclusion map are
skipped, and every escaping orbit is drawn with the renderer's colour ranges.
It uses `buddha++`'s option parser, exclusion map, checkpoint format and TIFF
writer, so the command line is the same:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target buddha-metal --parallel
./build/buddha-metal --width 8192 --height 8192 --scale 2048 --out render
./build/buddha-metal --width 8192 --height 8192 --scale 2048 --load render.zst   # resume
```

Press `Ctrl-C` (or send `SIGTERM`) to stop and save. If the exclusion map
file does not exist it is generated with the renderer's code and saved; an
existing file that cannot be loaded (for example another `--exclusion-size`)
is never overwritten. `--threads` sets only the checkpoint compression
threads.

How it differs from `buddha++`:

- **Sampler.** Uniform sampling, not Metropolis chains, so the image looks
  different (short orbits weigh more), and zoomed-in views waste most samples
  on orbits that never reach the window. Checkpoints validate geometry and
  iteration ranges, not the sampler: do not mix `buddha++` and `buddha-metal`
  checkpoints.
- **Precision.** Apple GPUs have no hardware double precision, so orbits,
  periodicity checks and pixel mapping use `float`. At full view this is
  visually identical to double (see the benchmark below); deep zooms are
  untested.
- **Formula.** Only `z = z * z + c`.
- **Memory.** The histogram lives in a shared Metal buffer and is copied into
  the renderer's histogram to save, so it needs about twice the histogram size
  in memory (768 MiB at 8192²). Indices are 32-bit, which allows up to about
  53,500² for windows on the real axis (mirrored histogram) or 37,800²
  off-axis.

The kernel is in `render_kernel.h` and its host side in
`persistent_renderer.h`. Each dispatch uses a fresh random key: sample `n`
takes `c = (hash(2n ^ key0), hash(2n + 1 ^ key1))`. Pass 1 applies the
renderer's escape and periodicity rules; orbits are not stored, so pass 2
re-iterates escaping orbits to draw points `low..orbitMax` with atomic adds,
using the same mapping and odd-height centre-row weight as
`buddha_generator::drawPoint`. It runs as persistent threads (see below).

On an M5 Pro, a 30-second 8192² render filled the histogram at 1.5 G points/s
(936 M samples/s), compared with about 0.35 G points/s for the CPU renderer
path in the benchmark.

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

- **CPU double (renderer path)** uses the renderer's own code: `mandelbrot`
  exclusion lookup, the double-precision periodicity loop into a stored orbit,
  and `buddha_generator::drawPoint` into the shared atomic histogram. It is
  built with the project flags, like `buddha++`.
- **CPU float (GPU algorithm)** runs the Metal kernel's algorithm on the CPU,
  for a like-for-like hardware comparison.
- **Metal float** cannot store an orbit per thread, so pass 1 tests escape and
  periodicity and pass 2 re-iterates escaping orbits to draw them with device
  atomics. Apple GPUs have no hardware double precision.

```sh
cmake --build build-metal --target metal-render-benchmark --parallel
./build-metal/metal-render-benchmark --samples 1000000000 --exclusion-map exclusion.map
```

A missing map is generated with the renderer's code and saved to that path;
`--exclusion-map none` disables it. Defaults match the renderer's colour
ranges (red 512–8192, green 128–2048, blue 32–512) at 8192×8192, scale 2048.
Histograms are compared per bin and on 16×16 pixel blocks; per-bin differences
are expected because long orbits are chaotic in either precision.

### Results

Apple M5 Pro, 15 CPU threads, 1e9 samples, 4096 exclusion map, two runs
agreeing within 2%:

| Path | Time | Samples/s | Histogram increments/s |
| --- | ---: | ---: | ---: |
| CPU double (renderer path) | 4.60 s | 217 M | 349 M |
| CPU float (GPU algorithm) | 4.59 s | 218 M | 350 M |
| Metal float, one sample per thread | 2.49 s | 402 M | 646 M |
| Metal float, persistent threads | **1.07 s** | 936 M | 1,502 M |

All images agree on 16×16 blocks (correlation 0.99993, under 1% relative L1),
so float precision does not visibly change a naive render at this scale.

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

`render_persistent` gives each thread many samples. It runs as a state machine
advancing one orbit step per loop iteration: a lane whose orbit escapes or
turns periodic loads its next sample in the same iteration, and a lane in the
redraw pass keeps drawing. Divergence is limited to the short load and draw
branches.

The thread count matters (`--gpu-threads`, `--persistent-batch`; 1e9 samples):

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
refill runs per thread and less histogram contention. Both kernels still spend
extra steps re-iterating escaping orbits (about half as many again as pass 1)
because orbits are not stored on the GPU.
