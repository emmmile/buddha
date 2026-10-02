# Session notes: September 2026 (CPU and Metal work)

Handover for the next session: what happened, where things stand, and what is
left.

## History

1. **PR #8 (CPU orbit loop, by another agent), closed.** Reviewed: the scalar
   orbit loop made the kernel 2.4× faster but the full generator only 1.33×.
   Profiling a real render showed histogram writes, not orbit evaluation,
   dominate at 8192². Closed without merging.
2. **Holding-time Metropolis experiment** (local branch
   `exp/mh-holding-weights`). Draws each accepted state weighted by how long
   the chain stays there, instead of drawing every proposal: 6.7× fewer orbit
   draws, 1.40× faster, but the image looked too contrasted. Not pursued.
3. **Metal exploration.**
   - The orbit loop alone is about 6× faster on the GPU than on 15 CPU
     threads.
   - A realistic naive render (exclusion map, periodicity check, histogram)
     was only 1.85×, because 32-thread SIMD groups wait for their longest
     orbit.
   - Persistent lanes, which refill as soon as an orbit ends, reached about
     4.3×.
   - Metropolis on the GPU was about 8.5× slower than the CPU (very uneven
     chain lengths), so it was set aside for now.
4. **Merged PRs:**
   - **#10 CI.** GitHub Actions on macOS: the lint step runs
     `.githooks/pre-commit` over the change since the base, then build and
     `ctest`.
   - **#11 Remove `--formula`.** The option was dead code; the checkpoint
     format was unchanged.
   - **#9 `buddha-metal`.**
     - A GPU renderer using naive (uniform) sampling in float. It shares
       `buddha`'s options, exclusion map, checkpoints and TIFF output.
     - `core/buddha_kernel.h` compiles as both C++ and Metal. It holds the
       rendering rules (used by `buddha++` too) and the sampling lane.
     - CPU and GPU results are bit-identical (`BUDDHA_EXACT`, and
       `fp contract(off)` in `render.metal`).
     - `buddha++ --sampler naive` runs the same sampler on the CPU.
     - Checkpoint version 2 records the sampler and refuses a mismatch;
       version 1 checkpoints load as Metropolis.
     - The page-aligned histogram (`page_allocator`) lets the GPU render into
       it without a copy.
     - The GPU runs 2,048 threads per core, with the core count read from
       IOKit.
     - Tests: `kernel-consistency` and `metal-consistency`.
     - The benchmarks are kept in history at commit `37787f4` (linked from
       `metal/README.md`).

## Key numbers (M5 Pro, 16 GPU cores, 8192², scale 2048)

- Naive render, 1e9 samples: CPU renderer code 4.72 s, Metal 1.12 s.
- `buddha-metal` render: about 900 M samples/s, 1.45 G histogram points/s.
- `buddha++` single-thread Metropolis: 59 → 77 M steps/s after the kernel
  refactor.
- Peak memory: `buddha-metal` 706 MB, `buddha++` 655 MB.

## What is left

- **Blending naive and Metropolis renders.** The preferred design keeps
  separate histogram layers per sampler in the checkpoint and mixes them at
  TIFF time (for example `--mix`). The alternatives (allow mixing with a flag;
  importance-weight Metropolis to match naive) were weaker.
- **Zoomed views on the GPU.** Naive sampling wastes samples when zoomed in.
  Idea: importance sampling from a grid of starting-point cells, or seeding
  Metropolis chains from GPU samples.
- **Other GPUs.** Untested: the 2,048 threads per core come from one
  machine.
- **Deep zooms in float.** Untested.
- **CPU histogram writes** are about 40% of CPU render time. Ideas: batch
  writes per thread, or per-thread tile accumulators.
- **PR #8's scalar orbit loop** is on hold. Revisit it only if the CPU loop
  matters again.

## Local leftovers (not deleted)

- `.worktrees/metal-memory` (`feature/metal-renderer`, merged): it holds
  renders (`exclusion.map`, `metal.*`, `metropolis.*`, `normal.*`). Keep or
  remove.
- `.worktrees/mh-holding` (`exp/mh-holding-weights`): unpushed experiment.
- `.worktrees/preview` (`preview`): obsolete preview of #7 and #8.
- `archive/metal-metropolis`: local-only archive of the Metropolis-on-Metal
  prototype.
- Remote branch `prototype/metal-orbit-benchmark` and the
  `buddha-metal-benchmark` worktree: superseded.
- Local branches whose remote is gone: `chore/cpp23-commit-checks`,
  `feat/scaled-point-logs`, `master`, `replace-timer-submodule`.
- Stash `local-naive-run-switch-20260927144415`: a local `run()` edit
  switching to naive, superseded by `--sampler naive`.

## Useful commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/buddha-metal -w 8192 -h 8192 -s 2048 -e exclusion.map -o metal   # Ctrl-C saves
./build/buddha++ --sampler naive -w 8192 -h 8192 -s 2048 -e exclusion.map -o naive
git config core.hooksPath .githooks                                      # lint on commit
```
