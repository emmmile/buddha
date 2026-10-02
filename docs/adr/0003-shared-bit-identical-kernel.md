# 0003. CPU and Metal share `core/buddha_kernel.h` and stay bit-identical

**Status:** Accepted
**Date:** 2026-09-27

## Context

`buddha-metal` (GitHub PR #9, commits `ab3cad2` and `d90124d`) added a GPU
renderer next to `buddha++`. Both read the same options, exclusion map and
checkpoints and write the same TIFF. Two copies of the rendering rules (escape
test, periodicity schedule, exclusion lookup, pixel mapping and mirroring,
channel ranges) would drift, and a GPU render could then not be checked
against, or continued by, the CPU.

Sources: [`metal/README.md`](../../metal/README.md) ("Shared kernel",
"Metropolis chains"), the
[session notes](../notes/SESSION_NOTES_2026-09.md), and
[`plans/METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md) ("Design:
Metropolis lanes").

## Decision

- `core/buddha_kernel.h` compiles as both C++ and Metal Shading Language. It
  holds the rendering rules, used by `buddha++` too, and the sampling lanes
  (naive, and since PR #17 the Metropolis `chain`).
- Shared arithmetic rounds every operation separately (`BUDDHA_EXACT`, and
  `#pragma clang fp contract(off)` in `render.metal`), so the CPU (in float)
  and the GPU compute bit-identical results. Metropolis uses Cephes
  polynomials for log, sin and cos, built from `+`, `-`, `*` and integer
  operations, for the same reason.
- `kernel-consistency` and `metal-consistency`, both run in CI, require
  identical counters and histograms.

## Consequences

- `buddha++ --sampler naive` runs the same sampler as `buddha-metal`, so each
  can continue the other's checkpoints.
- A GPU change can be validated against the CPU bit for bit. The Metropolis
  plan relies on this to separate precision and random-number effects from
  algorithm changes.
- Exact rounding cost the GPU about 3% (metal/README). Library math functions
  cannot be used in the shared lanes.
- The kernel is limited to what both languages accept, and to float where the
  GPU runs it.
- The WebGPU explorer is outside this guarantee: its WGSL is a port, not the
  shared header ([ADR 0001](0001-web-explorer-is-a-separate-product.md)).
