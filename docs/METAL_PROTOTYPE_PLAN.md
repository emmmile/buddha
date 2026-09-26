# Metal Histogram Prototype Plan

## Goal

Decide whether Apple-silicon GPU compute can accelerate the Buddhabrot
histogram phase before porting the full Metropolis sampler.

The existing `gigapixel` renderer is CPU-only in practice. Its remaining
`convertImage.cl` kernel is an unused final colour-conversion pass, not the
Monte Carlo generator.

## Non-goals

- Reproducing the exact final image in the first prototype.
- Porting Metropolis mutation, selection, and acceptance logic before the
  histogram strategy is validated.
- Replacing the CPU renderer until output quality and speed are measured.

## Baseline

The CPU renderer on this machine, using the historical RGB iteration defaults,
15 threads, a 4,096² exclusion map, and a 32,768² histogram with a `[-2, 2]`
square view, reached:

- 486.8 M evaluated orbit points/s
- 762.7 M histogram points/s

The 32,768² three-channel `uint32` histogram requires about 6 GiB. The CPU
benchmark reported about 55 minutes for 1.6 T evaluated points, or about 35
minutes if that count means histogram entries.

## Prototype 1: direct atomic histogram

Implement a small Objective-C++ / Metal command-line target. Keep the C++
settings format where practical, but do not depend on the existing generator.

Each Metal thread should:

1. Seed a deterministic per-thread PRNG.
2. Pick an independent complex starting point `c`.
3. Iterate `z = z * z + c` up to the existing channel limits.
4. Map in-window orbit points to pixels.
5. Atomically increment three `uint32` histogram channels using the existing
   RGB threshold semantics.

Use a shared `MTLBuffer` for the histogram, so the CPU can inspect it after a
completed command buffer without a discrete-GPU readback copy. Keep the first
version in `float`; compare it with CPU output at the intended view and
iteration ranges before treating it as quality-equivalent to the current
`complex<double>` implementation.

## Measurements

For a fixed seed count and fixed orbit-iteration work, benchmark 3,000×2,000,
8,192², 16,384², and 32,768² histograms.

Record for each run:

- wall time, orbit evaluations/s, and histogram updates/s;
- GPU command-buffer time and peak allocated buffer size;
- number of atomic updates and the distribution of updates per pixel;
- visual comparison with a CPU sample using the same seeds.

Run a no-write orbit-only kernel as a control. The difference between it and
the atomic version quantifies histogram-update cost directly.

## Prototype 2: reduce contention only if needed

If direct atomics are clearly limiting throughput, compare two alternatives:

1. Per-threadgroup partial histograms for image tiles, followed by a merge
   kernel.
2. Emitting pixel/channel indices in batches, sorting or reducing them on the
   GPU, then applying consolidated histogram increments.

Prefer the simplest approach that improves end-to-end histogram updates/s.
Large images reduce average same-pixel contention, but the Buddhabrot has
bright hot regions, so this must be measured rather than assumed.

## Decision gate

Continue to a full GPU sampler only when the prototype:

- preserves the expected image structure at representative settings;
- achieves a material end-to-end advantage over the CPU baseline; and
- has a histogram strategy that remains viable at 32,768² within the 48 GB
  unified-memory budget.

## Outcome

A fixed-trace and a GPU-owned Metropolis prototype were built and measured:
one chain per GPU thread was about 8.5× slower than the CPU, because natural
chain lengths are very uneven (median 2,560 proposals, 95th percentile
65,536). Porting the Metropolis sampler was dropped as too hard to make
GPU-efficient, and the prototypes were removed.

Instead, `buddha-metal` renders with naive uniform sampling, the exclusion map
and the periodicity check, using persistent GPU threads. It fills an 8192²
histogram about four times faster than the CPU renderer path. See
`metal/README.md`. The CPU Metropolis renderer remains the reference.
