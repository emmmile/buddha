# CPU performance benchmarks

Build Release before measuring:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`buddha-benchmark` is built independently of `BUILD_TESTING`. It never writes
images, checkpoints, or exclusion maps. `--seed` belongs only to this target;
`buddha++` retains its randomized render streams.

## Modes

### Orbit evaluation

Compare the production evaluator against the original complex-arithmetic loop:

```sh
./build/buddha-benchmark --mode orbit --kernel reference --seed 1
./build/buddha-benchmark --mode orbit --kernel production --seed 1
```

Both execute the same input points, iteration thresholds, contribution checks,
escape test, periodicity test, and sequence stores. The reference lives in
`test/orbit_reference.h`, separately from production code. Floating-point
contraction is disabled in the benchmark so the reference's complex arithmetic
does not acquire fused operations that were separate in the original
AppleClang renderer. The optimized renderer loop also prevents that fusion on
Clang. This matters: a last-bit change can alter a long orbit and the subsequent
Metropolis chain.

The corpus is generated before timing. `--corpus uniform` samples `[-2, 2]²`;
`--corpus boundary` jitters the difficult starting points from the historical
benchmark; `--corpus mixed` alternates between the two. These workloads exercise
different proportions of short escaping, long escaping, periodic, and
iteration-limited orbits. The default is mixed, not an estimate of the
renderer's exact proposal distribution.

`--samples` is the number of input points per logical chain. `--rounds` repeats
that corpus. The total number of evaluated input orbits is
`chains * samples * rounds`. Orbit mode allocates no histogram, even when large
image dimensions are supplied.

### Full generator

```sh
./build/buddha-benchmark --mode generator --seed 1 \
  --chains 16 --rounds 8 --proposal-limit 16384 \
  --width 8192 --height 8192 --scale 2048 --threads 8
```

This runs the production seed search, mutations, acceptance decisions, orbit
evaluation, synchronization, and shared atomic histogram writes.

Each logical chain runs `--rounds` Metropolis segments. Each segment retains
the renderer's normal termination condition, with an additional benchmark
ceiling of `--proposal-limit` proposals. Segments can finish early, so the
reported proposal count is the actual work, not necessarily
`chains * rounds * proposal-limit`. Seed search remains bounded at 256 attempts
per segment. The renderer does not use the benchmark ceiling.

Logical chains receive seeds derived from `--seed` and their chain index.
Changing `--threads` changes scheduling, not chain seeds or work allocation.
Keep `--chains` and all other settings fixed to compare thread counts. Only
`min(threads, chains)` workers are started. Repeatability assumes the same
binary/toolchain, rendering settings, and exclusion-map contents; standard
library random distributions are not a cross-platform checkpoint format.

### Histogram replay

```sh
./build/buddha-benchmark --mode histogram --seed 1 \
  --samples 1024 --rounds 16 --threads 8 \
  --width 8192 --height 8192 --scale 2048
```

Escaping orbits and their RGB draw commands are recorded before timing. The
timed section replays them through the production `drawPoint` implementation
into a shared histogram, isolating mapping and atomic writes from orbit
evaluation and sampling. Replay storage can grow with samples and iteration
limits; its allocated bytes are reported. The `drawn` counter is replayed
points in this mode and drawn orbits in generator mode.

## Measurements and comparisons

Every invocation runs one unreported timing warmup, followed by `--repeats`
measured repetitions (default five). The `warmup` line reports the work
signature. Every subsequent repetition must produce exactly that signature,
including histogram and orbit checksums, or the program fails.

Timing includes worker creation/join and the selected work. It excludes input
generation, replay preparation, allocation, buffer clearing, checksum scans of
the histogram, and file output. Per-orbit counters and the small orbit checksum
are included in orbit timing. Increase rounds if thread startup dominates a
very short run. Results include each repetition and median/minimum/maximum
wall time; run competing versions sequentially on an otherwise idle machine.

- `evaluated` counts recurrence steps, including work on discarded proposals.
  Exclusion-map hits perform zero recurrence steps. Historical renderer logs
  omitted some discarded proposal work and are not directly comparable.
- `contributions` is the orbit evaluator's visibility count, including points
  that may later be discarded. It is not the histogram total.
- `increments` is the sum of histogram channel counts, including symmetry
  weights. It is not the number of orbits or atomic instructions. Keep runs
  short enough to avoid wrapping individual `uint32` histogram counters.
- `peak_rss_bytes` is the process peak resident memory, including preparation
  and warmup. `histogram_bytes` and `replay_bytes` describe those allocations.

Exclusions are disabled by default. To include them, explicitly pass
`--exclusion-map path --exclusion-size 4096`. A missing or incompatible requested
map is an error. Use the same map for every comparison; map generation uses
random refinement and is outside the benchmark.

Use multiple seeds and views. Examples of additional geometry:

```sh
# Narrow off-axis view; the full-height histogram also exercises different mapping.
./build/buddha-benchmark --mode generator --seed 7 --threads 8 \
  --cre -0.5 --cim 0.6 --scale 2048

# Long iteration limit.
./build/buddha-benchmark --mode orbit --seed 7 --red-max 32768

# Gigapixel, symmetric histogram: 6 GiB for the histogram alone.
./build/buddha-benchmark --mode generator --seed 1 --threads 8 \
  --width 32768 --height 32768 --scale 8192
```

## Current optimization and validation

The production counted evaluator keeps real/imaginary coordinates in scalar
registers and reuses their squares for the escape test and next coordinate.
It retains visibility and periodicity checks, the complete recorded orbit,
iteration boundaries, double precision, and existing sampler behavior.

Two further experiments were not promoted: deferring visibility to the existing
uncounted evaluator changed orbit results through floating-point contraction;
specializing symmetric visibility and accumulating contributions locally slowed
the measured full generator. Neither is exposed as a renderer option.

The regression suite compares every recorded coordinate and all evaluation
outputs against the reference for random and difficult points, short iteration
caps around periodicity checkpoints, full and narrow views, off-axis windows,
and points on and adjacent to image boundaries. It also checks discarded-work
accounting, exclusion outputs, and identical benchmark results across worker
counts in all three modes. These checks supplement the existing image geometry
and checkpoint regression tests.

## Measured results

Local arm64 macOS, AppleClang 21, Release with native optimization and LTO,
2026-09-25. Each number is the median of three measured repetitions after one
warmup. Exclusions were disabled. These are elapsed times for fixed workloads,
not estimates of image convergence or guaranteed speedups on other machines.

| Workload | Before / reference | Optimized | Speedup |
| --- | ---: | ---: | ---: |
| Orbit, mixed corpus | 2.438 s | 1.162 s | 2.10× |
| Orbit, mixed corpus, off-axis zoom | 2.519 s | 1.223 s | 2.06× |
| Orbit, uniform corpus | 0.205 s | 0.098 s | 2.09× |
| Generator, 1024², 1 worker | 2.856 s | 2.068 s | 1.38× |
| Generator, 8192², 8 workers | 1.012 s | 0.780 s | 1.30× |
| Generator, off-axis zoom, 8 workers | 0.518 s | 0.399 s | 1.30× |
| Generator, negative-imaginary zoom, seed 7 | 0.516 s | 0.385 s | 1.34× |

All pairs had identical work signatures. The four full-generator comparisons
used a saved executable built before the orbit change, with the same benchmark
harness and corrected accounting. Their proposal/acceptance/drawing/search
counters, recurrence counts, histogram totals, and histogram checksums matched
exactly. Orbit comparisons use the included reference kernel.

Commit `484bcf3` provides a buildable full-generator baseline: it includes the
benchmark harness and accounting corrections, before the scalar orbit change.
Build that commit in a separate checkout and run the same generator commands
against both executables. Both the baseline and optimized version pass the
three regression tests on the measured toolchain.

To reproduce the workload definitions, add `--repeats 3` to each invocation:

- Orbit mixed: `--chains 4 --rounds 8` (other defaults).
- Orbit zoom: additionally `--cre -0.5 --cim 0.6 --scale 2048`.
- Orbit uniform: `--chains 4 --rounds 64 --corpus uniform`.
- Generator 1024²: `--mode generator --chains 16 --rounds 4 --threads 1`.
- Generator 8192²: `--mode generator --chains 16 --rounds 8 --threads 8
  --width 8192 --height 8192 --scale 2048`.
- Generator zoom: `--mode generator --chains 16 --rounds 8 --threads 8
  --cre -0.5 --cim 0.6 --scale 2048` (1024² defaults).
- Generator seed 7: `--mode generator --chains 16 --rounds 4 --threads 8
  --cre -0.5 --cim -0.6 --scale 2048 --seed 7`.

The isolated loop improvement is larger than the complete renderer improvement
because mutation generation, search, synchronization, and histogram writes
remain part of the full-generator workload.
