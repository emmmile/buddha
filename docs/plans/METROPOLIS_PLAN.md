# Metropolis sampler plan

> Status (October 2026): partly done, and the plan is kept for its reasoning.
> What shipped since:
>
> - **Metropolis lanes** in `core/buddha_kernel.h` (`chain`) and a `metropolis`
>   Metal kernel, bit-identical on CPU and GPU (`metal-consistency`), with
>   rules 1–5 and the Phase 2 knobs (seeding, exponents, radius, chain
>   length) in the native browser UI: GitHub PR #17 (`5fccebb`). Seeds must
>   now escape, which fixes the periodic-seed problem in the lane.
>   `buddha-metal` does not use the lanes, and `buddha++` has no
>   `--sampler chain`; its Metropolis sampler is still the legacy one.
> - **The WebGPU explorer** (GitLab MR !1, `0bd8588`) replaced the native
>   browser UI (`metal/browser_prototype.mm`, later `buddha-browser`) this
>   plan used for comparisons. It runs a WGSL port of the lane.
> - **Newton seeding** in the explorer only (GitLab MR !3, open): a new
>   answer to rule 5 and to the "seed pool for zooms" item below. See
>   [`research/SEEDING_AND_ZOOM.md`](../research/SEEDING_AND_ZOOM.md).
>
> Phase 0 (the legacy sampler in the UI) and Phase 3 (layers mixed at TIFF
> time) were not done.

## Goal

Replace the Metropolis sampler with one that runs on the GPU and much faster on
the CPU, keeps the look of the current renders, and can run alongside the naive
Metal renderer.

The current sampler is not a correct Buddhabrot estimator and does not need to
become one. Its value is the look it produces: deep iterations, dreamy
high-contrast mini-brots outside the main set, and zooms that do not waste
samples. The new sampler may change the algorithm freely as long as the images
look similar.

## Non-goals

- An unbiased Buddhabrot (that is what the naive sampler is for).
- Continuing legacy Metropolis checkpoints with the new sampler.
- Deep zooms beyond float precision (see "Later").

## What defines the look

`buddha_generator::metropolis()` (`core/buddha_generator.cpp`) is a
Metropolis–Hastings chain with these rules:

1. **Target.** `f(c) = L² · C`, where `L` is the orbit's escape iteration and
   `C` the number of orbit points inside the window. Periodic, excluded or
   non-contributing proposals (`C = 0`) are skipped.
2. **Proposal.** `c' = c + polar(Exp(2) · U · 20 / scale, U · 2π)`: isotropic,
   so symmetric, with a radius of about 20 pixels at any zoom.
3. **Drawing.** Every non-skipped proposal is drawn once, whether or not it is
   accepted. Rejected steps do not redraw the current state, and nothing
   reweights the samples. This is what makes the image differ from both the
   Buddhabrot and `f`, and it is where the look comes from.
4. **Chain length.** `max(256 · C₀, 2 · L₀)` proposals, from the seed's `C₀`
   and `L₀`.
5. **Seeding.** `findPoint`: a Gaussian random walk (step `|N(0,1)|`) from the
   origin, keeping the first point with `C > 0`, giving up after 256 steps.
   Seeds are not drawn.

The new sampler keeps rules 1–4. Rule 5 may change (see Phase 2).

## Known problems in the current code

- `findPoint` accepts a periodic seed (`C > 0`, `L = -1`); the first acceptance
  ratio then divides by `(-1)² · C`.
- `mandelbrot::evaluate` returns early for excluded points without setting
  `contribute`, so the caller sees a stale value. It is harmless only because
  of the order of the checks.
- `computed` skips periodic proposals, so the reported rates are low.
- Every proposal takes the thread's mutex; the orbit lives in a `seq` vector
  of `std::complex<double>`.
- Seeds from the random walk cluster near the origin and mostly fail when
  zoomed in.
- On the GPU (`archive/metal-metropolis`), one thread ran one whole chain.
  Chain lengths range from a median of 2,560 to a 95th percentile of 65,536
  proposals, so SIMD groups and dispatches waited for the longest chain: about
  8.5× slower than the CPU.

## Design: Metropolis lanes

Add a Metropolis lane to `core/buddha_kernel.h`, next to the naive lane, so the
CPU and GPU share one definition and stay bit-identical, as the naive sampler
does.

- **One orbit step per `advance()`.** When a proposal's orbit ends, the lane
  decides whether to accept it, starts the next mutation and continues, in the
  same step. A lane never waits for another lane's chain.
- **Chains persist across dispatches.** A lane's state (current `c`, `L`, `C`,
  proposals left, current proposal and orbit position, mode, random counter)
  is saved in a buffer at the end of a dispatch and loaded at the start of the
  next. A dispatch runs a fixed budget of orbit steps per lane rather than
  "finish your chain", so uneven chain lengths stop mattering.
- **Two passes, like the naive lane.** Pass 1 iterates the proposal to get
  `L`, `C` and the periodicity result; pass 2 re-iterates an escaping,
  contributing proposal to draw it. No orbit is stored.
- **Counter-based random numbers.** Draws come from `mix(counter ^ key)` per
  lane, as in the naive sampler. Mutations use 24-bit uniforms: the
  exponential by inverse CDF, the angle from a uniform.
- **Seeding from uniform samples.** A lane without a chain draws points
  uniformly over `[-2, 2]²` until one escapes with `C > 0`, then starts a
  chain from it. This replaces the random walk and is the "seed Metropolis
  chains from GPU samples" idea in the session notes.
- **Float.** The same precision as the naive lane. The ratio `L² · C` fits
  comfortably in float.

On the CPU, `buddha++` runs the lane in a loop per thread, like
`buddha_generator::naive()`. This removes the `std::complex`, `seq` vector and
per-proposal mutex overheads. Histogram writes (about 40% of CPU render time)
are unchanged.

Checkpoints do not record chain state; resuming restarts the chains, as it
does today. The new sampler gets its own name while it is being validated (for
example `--sampler chain`); `metropolis` keeps the legacy sampler as the
reference.

## Phases

Each phase ends with something runnable and a comparison against the previous
one. Renders and benchmarks are run by hand, not automatically.

The browser UI (`metal/browser_prototype.mm`, PR #13) is where the samplers are
compared and tuned: panning, zooming and changing a setting there is much
faster than a command-line render. It currently runs only the naive sampler on
Metal, so the phases bring each sampler into the UI as soon as it exists.

### Phase 0: the legacy sampler in the UI

Add a sampler setting to the UI, and a CPU backend that runs `buddha`'s own
generators (`startGenerators` / `stopGenerators`) into the session's histogram.
The preview and display path already read that histogram, so the legacy
Metropolis sampler (and the CPU naive sampler) appear in the UI with little new
code.

Add cheap counters to `metropolis()` and show them in the status line:
acceptance rate, chain lengths, seed attempts and failures, and a log₂
histogram of `L` for drawn orbits. Fix the `computed` count. Do not change the
sampling.

Result: the reference look, visible at any view in the UI, with numbers that
describe it.

### Phase 1: Metropolis lanes on the CPU and GPU

Implement the Metropolis lane in `buddha_kernel.h` with rules 1–5, including
the random-walk seeding. Fix the periodic-seed and stale-`contribute` problems
in the lane only; the legacy sampler stays as it is.

Run it on both sides at once, because the UI renders on the GPU:

- a CPU sampler in `buddha++` (`--sampler chain`), as the reference;
- a Metal kernel in `render.metal` with the chain-state buffer and
  step-budgeted dispatches, used by `buddha-metal` and the UI.

Tests: unit tests for the acceptance rule, the chain length and seed
rejection; `metal-consistency` checks that a small GPU render matches the CPU
lane bit for bit.

Result: the legacy and new samplers side by side in the UI, with the same
counters. This isolates float precision, the new random numbers and the bug
fixes from any change of algorithm. Measure proposals/s, orbit steps/s and
histogram points/s against the naive Metal renderer and tune lanes per core
and the step budget; expected throughput is close to the naive renderer's.

### Phase 2: seeding and knobs

Add the sampler's settings to the UI, each starting a new histogram when it
changes:

- seeding: random walk or uniform;
- the exponents of `f = L^a · C^b` (to push the contrast up, or back toward
  naive);
- the mutation radius, in pixels;
- the chain-length factor.

Use them to decide whether uniform seeding keeps the look, and which defaults
the sampler should have.

### Phase 3: run alongside naive

This merges with the "blending naive and Metropolis renders" item in the
session notes: one histogram layer per sampler in the checkpoint, mixed at TIFF
time (`--mix`) and with a mix setting in the UI's display controls. On the GPU,
both run in the same process, as separate dispatches or as a split of lanes in
one dispatch. Each extra layer at 8192² costs about 800 MB.

## Later

- **Seed pool for zooms.** Keep contributing seeds found by any lane and start
  new chains from them, so zoomed renders stop paying for the search.
- **Double path.** Float mutations stop resolving once the mutation radius
  approaches float precision around `c`, roughly at scale 10⁶. A double lane
  on the CPU (or emulated on the GPU) would be needed there.
- **Retire the legacy sampler** once the new one replaces it for your renders,
  and decide whether it takes over the `metropolis` name.

## Open decisions

- Whether rule 5 (random-walk seeding) is part of the look. Phase 2 answers it.
- Which knobs stay once the defaults are chosen.
- The sampler's final name.
