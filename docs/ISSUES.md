# Issues

Known problems, with what we know about each and a proposed fix. Ideas that
aren't problems go in [`IDEAS.md`](IDEAS.md).

## Numerical artifacts below span ≈ 0.001 (explorer)

**Status:** open. Found by the user in October 2026 while trying Newton
seeding (`feature/newton-seeding`, GitLab MR !3).

**Symptom.** In the WebGPU explorer, views with a span below about 0.001
(the shorter side of the view, in complex units) show numerical artifacts.
Above that they look fine.

**Why (analysis, not yet verified in the shader).** Everything is f32, and the
artifacts start much earlier than the existing warning (`1/scale < 1e-6`, in
`main.ts`) suggests:

- The spacing of f32 values for c in [1, 2) in magnitude (the antenna, around
  −1.78, and most of the plane) is 2⁻²³ ≈ 1.2·10⁻⁷. c can only take values
  on that grid.
- An orbit point z_k moves by about |dz_k/dc| times a change in c. For seeds at
  `#re=1.4&im=0&span=0.0148875`, the median |dz/dc| at the visit is 10^1.8 for
  uniform seeds and up to 10⁴ or more for Newton's (see "The look changes:
  seed bias" in
  [`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md)).
- So the orbit points one c can produce form a grid in the window with gaps of
  about 1.2·10⁻⁷ × |dz/dc|, roughly 10⁻⁵ to 10⁻³. At span 0.001 on a
  1189-pixel side, a pixel is 8.4·10⁻⁷, so the gaps are tens to hundreds of
  pixels. Many chains with different derivatives fill them in, which is
  probably why the problem shows as artifacts rather than an obviously broken
  image.
- The orbit itself also loses accuracy step by step. That matters less for a
  statistical image than the c grid does.
- The Newton seeding's convergence test (half a pixel) and the mutation radius
  (1% of the view, in c) also approach f32 spacing at these spans.

**Proposed short-term fix.** Limit the span to **0.005** for now, as the
user suggested. 0.001 is where artifacts showed, so 0.005 leaves a margin.
- Clamp in `geometry.ts`'s `setView` (wheel and pinch zoom), when applying a
  URL hash, and through the Span field's minimum.
- Replace the `1/scale < 1e-6` "Beyond single precision" message, which
  would no longer fire.
- The limit is a span (window size). Express it as a constant next to
  `FULL_SPAN` in `main.ts`, so it is independent of screen size.
- Keep it in one place, so it can later depend on the measured |dz/dc| (the
  "precision gauge" in the research doc) instead of a fixed number.

**Real fix.** More precision for c, and for z near the window: double-float
emulation (df64) on the GPU, or per-chain perturbation (research doc
section 5, "Beyond f32"). Until then, the limit keeps the explorer honest.

**To check.**

- Whether the artifacts depend on the region (they should get worse where
  |dz/dc| is large, and near |c| ≥ 2 or |z| ≥ 2, where f32 spacing doubles).
- Whether they depend on the seeding mode (Newton seeds have larger
  derivatives than walk seeds).
- Whether the limit should sit higher or lower than 0.005 once it is in
  place.

## Newton seeding over-weights the real axis (explorer)

**Status:** open. Found by the user in October 2026, measured the same day.
Newton seeding has been the default since GitLab MR !3, so this changes the
default look.

**Symptom.** At the full view and views near it, the real axis is a bright
line with glowing minibrots, off-axis minibrot ghosts are dim, and the body is
darker and more golden than with walk or uniform seeding. At
`#re=0&im=0&span=4`, the body is half as bright as with uniform seeding
(0.21 against 0.41), and the off-axis ghosts a third (0.14 against 0.39).

**Why.** `SOLVE_ANTENNA = 0.5` in `metropolis.wgsl` starts half of all Newton
attempts on the antenna, so about half the chains have real or nearly real c.
Their orbits lie on the real axis, which takes a large share of the counts.
The display scales to the brightest pixels, so everything else dims. With the
share at 0, Newton matches walk and uniform. Measurements and renders: "The
antenna starts over-weight the real axis" in
[`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md).

The share was added for `#re=1.4&im=0&span=0.0148875`, where 81% of good
seeds really are on the antenna. No fixed share suits both kinds of view.

**Proposed fix: hybrid seeding.** Each lane seeds with the walk (or uniform)
as before. It switches to Newton only after the walk has failed for a while,
and drops the antenna starts. Where walk seeds are found, the image keeps
the walk's look; at the full view Newton gains nothing there anyway (all modes
fill at 3.7–4.2G points/s). When zoomed in, the walk fails and Newton takes
over. The antenna starts would be unnecessary, or reduced to a small share
for windows on the real axis. Measure the result with
`research/experiments/capture.ts` and `axis_bias.py` at the full view, and by
eye at the re=1.4 view. The general fix is the start map in
[`IDEAS.md`](IDEAS.md).

## Display adjustments differ between the explorer and the CLI

**Status:** open. Confirmed as a goal by the user (October 2026): display
adjustments should ideally match between the browser and the CLI.

**Symptom.** A view exported from the explorer does not look the same when
rendered with `buddha++`. The explorer applies the tone curve (exposure and
gamma) and then saturation, texture and sharpness on the GPU
(`web/src/shaders/display.wgsl`). The CLI applies only the tone curve.

**History.** The native browser plan (in git history) made the adjustments
part of the image, applied to TIFF export through a shared native pipeline
(`core/image_pipeline.h`). The WebGPU explorer reimplemented them in WGSL, and
`e95748b` removed the native pipeline, so the CLI lost them.

**Proposed fix.** Port saturation, texture and sharpness to the CLI's TIFF
output with the same definitions as `display.wgsl` (the same blur radii and
passes, and the same luminance), and accept them as options. Then the
settings handed over from the explorer (the PNG metadata and URL hash, see
[ADR 0001](adr/0001-web-explorer-is-a-separate-product.md)) can be applied as
they are. The README's "Future work" settings file for `buddha++` is the
natural carrier. Bit-identical output is not required: the explorer's shader
is a port, not shared code.

## The GPU is only about half busy (explorer)

**Status:** open, not measured. Reported by the user: the GPU is about 50%
utilized on average while the explorer renders.

**Likely causes** ([research doc](research/SEEDING_AND_ZOOM.md), section 6), roughly by expected impact:

1. **Gaps between dispatches.**
   - Only two dispatches are in flight (`IN_FLIGHT = 2` in `sampler.ts`).
   - Each completion waits for a `mapAsync` readback, which resolves late in
     the browser's event loop, before the next dispatch is submitted.
2. **A fixed lane count.** `THREADS = 32768` was tuned for one Apple GPU
   (16 cores × 2048). `buddha-metal`'s 2,048 threads per core come from the
   same machine; no other GPU has been tried.
3. **Divergence.** Lanes in different chain modes (`REDRAW`, `EVALUATE_*`, and
   with MR !3 `SOLVE`) run different code within one SIMD group.
4. **Register pressure.** The `Chain` struct is 104 bytes with MR !3.

**First step.** Measure the duty cycle: the explorer's *GPU busy* measure (GPU
time from timestamp queries over wall-clock time) shows it directly.
In headless Chrome at the full view it read 98% for every seeding mode
(research doc, "The antenna starts over-weight the real axis"). The 50%
report predates the measure and may have come from the system's GPU history,
which measures something else. Check the measure in a visible window before
working on the causes below.

**Proposed fixes.**
- Queue 3–4 dispatches.
- Accumulate the counters on the GPU and read them back every N dispatches.
- Scale the lane count with the adapter.
- Share one orbit-step path between the chain modes.

## Known bugs in buddha++'s legacy Metropolis sampler

**Status:** open, kept on purpose. `buddha_generator::metropolis()` is the
reference for the look ([ADR 0002](adr/0002-metropolis-optimises-for-the-look.md)),
so these were fixed only in the newer Metropolis lane (`chain` in
`core/buddha_kernel.h`), not in the legacy sampler:

- `findPoint` accepts a periodic seed (`C > 0`, `L = -1`); the first acceptance
  ratio then divides by `(-1)² · C`.
- `mandelbrot::evaluate` returns early for excluded points without setting
  `contribute`, so the caller sees a stale value. It is harmless only because
  of the order of the checks.
- `computed` skips periodic proposals, so the reported rates are low.
- Every proposal takes the thread's mutex, and the orbit lives in a `seq`
  vector of `std::complex<double>`, which makes it slow.

They go away when the legacy sampler is retired (see "Metropolis lanes in the
CLI renderers" in [`IDEAS.md`](IDEAS.md)).
