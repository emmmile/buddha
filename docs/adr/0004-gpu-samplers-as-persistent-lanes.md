# 0004. GPU samplers are persistent lanes that advance one orbit step per call

**Status:** Accepted
**Date:** 2026-09-29

## Context

GPUs run 32-thread SIMD groups in lockstep, so a loop runs until the group's
slowest thread finishes. Two early designs hit this:

- One sample per thread for naive sampling: 2.49 s for 1e9 samples. Removing
  the histogram writes saved only 10%, so divergence, not atomics, was the
  limit.
- One whole Metropolis chain per thread (`archive/metal-metropolis`): about
  8.5× slower than the CPU, because chain lengths range from a median of
  2,560 to a 95th percentile of 65,536 proposals.

Sources: [`metal/README.md`](../../metal/README.md) ("Benchmarks",
"Metropolis chains"),
[`plans/METAL_PROTOTYPE_PLAN.md`](../plans/METAL_PROTOTYPE_PLAN.md)
("Outcome"), [`plans/METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md)
("Design: Metropolis lanes"), and commit `8113824`.

## Decision

Each GPU thread runs one lane, a state machine that performs one orbit step
per `advance()` call and moves to its next sample, proposal or chain as soon
as the current orbit ends. Lane state persists between dispatches, and a
dispatch gives every lane a fixed budget of steps rather than "finish your
work". Orbits are not stored: an escaping orbit is iterated again to draw it.

## Consequences

- Naive sampling went from 2.49 s to 1.07 s for 1e9 samples, and uneven
  Metropolis chain lengths stopped stalling SIMD groups.
- Re-iterating escaping orbits costs about half as many steps again.
- Lanes in different modes (evaluate, redraw, seek; Newton's solve in the
  explorer) still diverge within a SIMD group. The research doc lists this as
  a utilization cost (section 6).
- New sampler features have to be written as lane modes, one orbit step at a
  time. Newton seeding's `SOLVE` mode follows this (MR !3).
- The explorer's WGSL port keeps the same structure, with chain state in a
  storage buffer and dispatches sized toward 25 ms of GPU time.
