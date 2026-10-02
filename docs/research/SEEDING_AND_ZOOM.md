# Seeding and zoom research (October 2026)

Research notes for the WebGPU explorer (`web/`) after GitLab MR !1
(`feature/webgpu-explorer`, merged as `0bd8588`). Covers how to find Metropolis
starting points quickly at high zoom, better mutations, zoom UX, a starting-point
database, deep zoom, and GPU utilization. Written so a later session can pick up
any item without redoing the research.

## Ideas we started from

1. Encode settings in the URI, so sharing is easy.
2. A faster or better way to find initial points for Metropolis chains.
3. Faster sampling at high zoom in general, preferably GPU friendly.
4. Precomputed starting points in a database, updated over time.
5. The GPU is only about 50% utilized on average.

Item 1 is implementation work with no research needed (see "URI settings"). The
rest are below.

## The sampler as it is (for reference)

`web/src/shaders/metropolis.wgsl` is a port of the Metropolis lane in
`core/buddha_kernel.h`:

- 32,768 lanes (`THREADS` in `sampler.ts`), one chain per lane. Each
  `advance()` is one orbit step in a state machine:
  `SEEK → EVALUATE_SEED → PROPOSE → EVALUATE_PROPOSAL → REDRAW → PROPOSE …`.
- Chain state persists across dispatches. Dispatches are resized toward 25 ms
  of GPU time (`steps` per lane, 16 to 16,384), with two in flight.
- Orbit convention: **z₀ = c**, `z_{k+1} = z_k² + c`, escape at `|z|² > 8`.
  The window test (`pixel()`) is applied to every z_k, including z₀ = c.
- Target density `L^a · C^b` (`exponent_l`, `exponent_c`, default 1 and 1).
  L is the escape iteration and C the number of orbit points in the window.
  Every valid proposal is drawn, accepted or not (rule 3 in
  [`METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md)).
- Chain length `chain_scale · max(256·C₀, 2·L₀)` proposals.
- Mutation: `c' = c + Exp(1)·0.5·U·radius · direction`. `radius` is in complex
  units, set to `radius%` of the view's shorter side (default 1%).
- Seeding (`pick_seed`):
  - `walk`: a Gaussian-ish random walk from the origin (step `|N(0,1)|` from
    Irwin–Hall, uniform direction), reset after 256 steps (`WALK_LIMIT`);
  - `uniform`: uniform over [−2, 2]².

  Either way, a seed is kept if it is not excluded and its orbit escapes with
  L ≥ 2 and C > 0.
- Counters: `seeds` (candidates tried) and `chains` (chains started), so
  `seeds / chains` is the cost of finding a seed. `proposals`, `accepted`,
  `escaped` (drawn), `increments`, `iterations` and `redraw` are steps.
- Precision: f32 everywhere. `main.ts` warns "Beyond single precision" when
  `1/scale < 1e-6`.
- Channel limits by default: blue 32–512, green 128–2048, red 512–8192, so
  `p.low = 32` and `p.high = 8192`. Orbits with L ≤ 32 draw nothing.

## Prior work

- **Alexander Boswell's Metropolis–Hastings Buddhabrot**, the origin of this
  project's approach. Mutate trajectories that hit the view, because nearby
  starting points give similar trajectories.
  [superliminal.com/fractals/bbrot](https://superliminal.com/fractals/bbrot/),
  [causalloop.net](http://www.causalloop.net/blog/?p=88), C++14 port
  [Quuxplusone/Buddhabrot](https://github.com/Quuxplusone/Buddhabrot) with a
  [blog post](https://quuxplusone.github.io/blog/2021/07/27/buddhabrot/). That
  post notes blue-shifted colours from Metropolis's "splotchy" sampling.
- **Benedikt Bitterli, "Rendering a Buddhabrot at 4K and other bad ideas"**
  ([benedikt-bitterli.me/buddhabrot](https://benedikt-bitterli.me/buddhabrot/)):
  - An explicit 2D importance map of starting points. Each trajectory is rated
    by how many histogram bins it touched, and the ratings accumulate into a
    map that is refined over time.
  - Starting points are sampled from that map with the standard 1/pdf weight
    (unbiased). "The importance map is almost equivalent to an image of the
    Mandelbrot set: important trajectories almost all start right at the
    border."
  - Explicitly argues against Metropolis for animation: correlated samples
    give splotchy noise that flickers.
  - Denoising with non-local means. The variance comes from several
    histograms, each trajectory splatted to one of them. Animations add
    temporal filtering over ±5 frames.
- **FracturingFog issue #837, "zoom starvation"**
  ([issue](https://github.com/AloneButUnsober/FracturingFog/issues/837),
  [PR #840](https://github.com/AloneButUnsober/FracturingFog/pull/840)):
  - a sample budget that grows with zoom (capped at ×8);
  - Metropolis turned on automatically past zoom 1.2;
  - per-frame normalization by the total number of hits.
- **mathr (Claude Heiland-Allen)**:
  - [Buddhabrot variants](https://www.mathr.co.uk/web/m-buddhabrot.html), such
    as the "ultimate anti-Buddhabrot", which accumulates limit cycles found by
    Newton's method;
  - [nucleus](https://www.mathr.co.uk/web/m-nucleus.html),
    [atom domains](https://www.mathr.co.uk/web/m-atom-domains.html),
    [exterior distance](https://www.mathr.co.uk/web/m-exterior-distance.html),
    [external rays](https://www.mathr.co.uk/web/m-external-ray-in.html), and
    [deep zoom theory](https://mathr.co.uk/blog/2021-05-14_deep_zoom_theory_and_practice.html)
    (perturbation, BLA);
  - exterior distance estimate: `d = 2|z| log|z| / |dz/dc|`, with
    `dc ← 2·z·dc + 1`.
- **Equidistribution.** Parameters with special critical orbits (superattracting
  parameters, Levin; preimages; cycles with given multipliers) become
  distributed like the harmonic measure of ∂M as the period grows. See
  [arXiv 1306.2736](https://arxiv.org/pdf/1306.2736),
  [DeMarco](https://people.math.harvard.edu/~demarco/QPer1.pdf) and
  [arXiv math/0601612](https://arxiv.org/html/math/0601612). This is the
  theory behind "roots of `z_n(c) = w` lie near the boundary".
- **GPU renderers.** [yalue/cudabrot](https://github.com/yalue/cudabrot)
  samples over the whole domain, so zooming does not speed it up.
  [donghaoren/buddhabrot-renderer](https://github.com/donghaoren/buddhabrot-renderer)
  is real-time in WebGL 2.

I found no prior work that solves for seeds directly (Newton seeding, below).
The search was limited, so it may exist under another name.

## 1. Newton seeding: a formula for starting points

### Idea

A seed is useful when its orbit passes through the window. Pick a target w in
the window and a step n, and solve `z_n(c) = w` for c. With the shader's
convention:

```
z_0 = c,  z_{k+1} = z_k² + c
d_0 = 1,  d_{k+1} = 2·z_k·d_k + 1          (d_k = dz_k/dc)
c ← c − (z_n − w) / d_n                    (Newton step; clamp |step| ≤ 0.5)
```

`z_n(c)` is a polynomial of degree 2ⁿ in c, so there are 2ⁿ solutions. Newton
converges to one of them from a nearby start.

Closed forms for small n:

- n = 0: c = w (the seed is in the window itself).
- n = 1: `c² + c = w`, so `c = (−1 ± √(1 + 4w)) / 2`.

As n grows the solutions spread over ∂M (see equidistribution above). ∂M is
where the long, high-L orbits start, so large n gives better seeds but Newton
converges less often.

A solution is a good seed when it is not excluded, escapes with L > p.low (32),
and has C > 0. C ≥ 1 holds by construction once Newton has converged within
the window and z_n is reached before escape.

### Experiment

Python and numpy, f64, CPU. The scripts are `seeds.py`, `diag.py` and
`diag2.py` in [`experiments/`](experiments/) (see the appendix). Run them with
`uv run --with numpy python seeds.py`; numpy is not in the system Python.
Window: a square of half-width `half` around w. Good = L > 32 and C > 0 with
HIGH = 8192, matching the default channel limits. "Converged" = `|z_n − target| < half`
after 30 fixed Newton iterations. Newton starting points are uniform over
[−2, 0.5] × [−1.2, 1.2] unless noted.

Uniform seeding against Newton seeding (n uniform in [2, 40), random start):

| window centre w | half-width | uniform hit rate (200k samples) | Newton good rate | median L | median C |
|---|---|---|---|---|---|
| −0.1+0.75i | 1e-2 | 1.75e-4 | 4.4% | 62 | 1 |
| −0.1+0.75i | 1e-4 | 0 (expected ~2e-8, scaling by area) | 4.3% | 70 | 1 |
| 0.3+0.2i | 1e-4 | 0 | 3.4% | 54 | 1 |
| −1.2+0.15i | 1e-5 | 0 | 5.6% | 60 | 1 |

Newton cost was about 610 orbit steps per attempt (30 iterations × mean n), not
counting the final evaluation.

Why Newton attempts fail (w = −0.1+0.75i, half = 1e-4, 3,000 attempts):

| n range | start | converged | converged but inside M | converged but L ≤ 32 | good | median L (good) |
|---|---|---|---|---|---|---|
| [2, 40) | random | 23% | 5% | 14% | 3.6% | 77 |
| [2, 10) | random | 81% | 18% | 57% | 6.1% | 122 |
| [20, 60) | random | 4% | 1% | 0% | 2.5% | 68 |
| [2, 10) | boundary pool | 94% | 25% | 57% | 12.0% | 77 |
| [2, 40) | boundary pool | 56% | 8% | 23% | **25.5%** | 53 |
| [10, 40) | boundary pool | 46% | 4% | 12% | **30.0%** | 51 |

"Good" in the second table also requires convergence. The first table counts
any c ending good, which is why its rates differ slightly.

"Boundary pool" means starting points drawn from uniform samples over
[−2, 0.5] × [−1.2, 1.2] that escaped with L > 32: 1,342 of 60,000 samples, a
cheap first pass. Median C stayed 1 and max C was 2–18. Newton gives seeds
that touch the window, and the chain's `L·C` target then has to climb.

### Conclusions

- At windows of 2·10⁻⁴ and smaller, uniform or walk seeding effectively never
  finds a seed. Newton from the boundary pool finds one in 3–4 attempts. Cost
  per good seed is about 3k orbit steps for Newton, against about 10⁹
  (≈ 1 / 2·10⁻⁸ samples × tens of steps each) for uniform: 10⁵–10⁶× better.
- Small n converges reliably but its roots escape too early. Larger n gives
  boundary roots but converges less often. Starting near the boundary fixes
  much of that: n in [10, 40) from boundary starting points was best.
- 30 fixed Newton iterations is wasteful. Stop when `|z_n − w| < tolerance`
  and give up on divergence (non-finite or step stuck at the clamp), which
  should cut the cost several times.
- Not yet verified: f32 behaviour (the experiment was f64), the effect on the
  look, and how fast chains climb from C = 1.

### GPU design sketch

- A new seeding mode `newton` (`p.seeding == 2`), next to `walk` and `uniform`.
- New chain modes in the state machine, one orbit step per `advance()` as
  today:
  - `SOLVE`: iterate z and d for n steps from the current guess, then take a
    Newton step on c. Repeat up to K times or until converged; then
    `start(c)` and `EVALUATE_SEED` as usual.
  - Starting guess: a boundary point (see below), a fresh one per attempt.
- Per-attempt state in `Chain`: target w, n, guess c, d, Newton iteration
  count. Chain has two pad words, and the buffer can simply grow (`THREADS ×
  stride`).
- Target w: uniform in the window (`minre`/`minim` plus random × size). n:
  uniform in [N₀, N₁), with defaults 10 and 40.
- Boundary starting points, options:
  1. Cells of the exclusion map's boundary: cells not excluded with an
     excluded neighbour. A list built on the CPU at load and uploaded as a
     storage buffer; sample a cell, then a uniform point in it.
  2. A cheap random search for L > 32 (as in the experiment): this costs a
     few hundred steps per boundary point. Pool them.
  3. Previous good seeds or chain states (the seed pool, section 3).
- Overflow: |d_n| for n ≤ 40 near ∂M stays far below f32's 3.4e38 in
  practice. Guard against non-finite steps anyway.
- Precision: Newton converges to c within about `window / |d_n|`, which is
  finer than the window. In f32, around |c| ≈ 1 the spacing is about 6e-8. So
  deep windows (≲1e-5 with |d_n| in the hundreds) will lose convergence before
  `1/scale < 1e-6`. Measure it.
- Counters: `seeds` counts Newton attempts, so `seeds/chains` is directly
  comparable with walk and uniform. Consider a `solve` counter for Newton
  orbit steps, to compare costs in steps rather than attempts.

### Implementation (branch `feature/newton-seeding`, GitLab MR !3)

Commits `f743ca2` (this section) and `c7c20ee` (antenna starts, below).

- `seeding = newton` (`p.seeding == 2`) in the explorer's "Seeds" select.
- `metropolis.wgsl`: `begin_solve()` and `solve()` implement the `SOLVE`
  mode, one orbit step (z and dz/dc) per `advance()`.
  - Constants: `SOLVE_MIN = 10`, `SOLVE_MAX = 40`, `SOLVE_ROUNDS = 24`, Newton
    step clamped to 0.5.
  - Convergence: `|z_n − w| < 0.5 / scale` (half a pixel). The converged c then
    goes through the usual exclusion check and `EVALUATE_SEED`.
  - Attempts are abandoned when `|z|² > 1e16` (escaped), when a component of
    dz/dc exceeds 1e29 (before f32 overflow; |dz/dc| ≈ 4ⁿ near the boundary is
    normal, so do not cap it lower), when the step leaves `|c| < 4`, or after
    24 rounds.
  - The Newton step divides by d scaled by `max(|Re d|, |Im d|)`, so |d|²
    cannot overflow.
- Boundary starting points: `exclusion.ts` lists the cells that are not
  excluded but have an excluded 4-neighbour (binding 5, `boundary_count` in
  the old `pad1` of Params).
  - Only **19,521** of the 8192 × 4096 cells qualify (3.14M are excluded).
    The list is 76 KB and takes about 0.1 s to build in JS.
  - These cells follow the boundary of the main cardioid and the large bulbs
    only. Minibrots far from the main body are not excluded, so they are not
    in the list (a possible improvement: add cells around their nuclei).
  - The map stores only Im ≤ 0 (8192 × 4096 square cells over
    [−2, 2] × [−2, 0]). The other half is mirrored, so the start point
    takes a random sign for Im.
- A new counter, `solve` (Newton orbit steps). The debug line shows
  `seeds/chain` and `solve steps/chain`. `Chain` grew from 80 to 104 bytes.
- Not ported to `core/buddha_kernel.h` / `buddha++` yet.

### GPU smoke test (f32, Deno WebGPU, Apple GPU)

4,096 lanes, 2,048 steps per lane per dispatch, 8 dispatches, a 256 × 256
window centred on −0.1+0.75i, default channels, radius 1%. Script:
[`experiments/smoke.ts`](experiments/smoke.ts). Counts are totals over the run.

| span | seeding | chains | seeds/chain | solve steps/chain | drawn | increments |
|---|---|---|---|---|---|---|
| 1e-2 | walk | 245 | 163,695 | – | 12,321 | 1,591 |
| 1e-2 | uniform | 540 | 16,634 | – | 30,190 | 4,367 |
| 1e-2 | **newton** | 5,188 | **7.2** | 929 | 435,539 | 35,878 |
| 1e-3 | walk | 8 | 5,164,920 | – | 26 | 40 |
| 1e-3 | uniform | 4 | 2,406,132 | – | 579 | 0 |
| 1e-3 | **newton** | 4,620 | **10.9** | 1,550 | 407,376 | 14,195 |
| 2e-4 | walk | 0 | – | – | 0 | 0 |
| 2e-4 | uniform | 0 | – | – | 0 | 0 |
| 2e-4 | **newton** | 4,029 | **21.5** | 3,356 | 242,240 | 7,184 |

Newton seeding works in f32 and turns "no chains at all" into a chain per
lane within a dispatch. Histogram increments per orbit step fall with zoom
(0.56, 0.22, then 0.11 per 1k steps), because most new chains start at
C = 1. The next levers are faster climbing (section 2) and the seed pool
(section 3). Still to check by eye: whether the look changes against walk
seeding at zoom levels where walk works.

### The look changes: seed bias (view re=1.4, im=0, span 0.0148875)

**Observation (by eye, in the explorer).** At
`#re=1.4&im=0&span=0.0148875` there are deep minibrots and a fainter one in
the background. With walk seeding both are drawn. With Newton seeding
(n in [10, 40), boundary-cell starts) only the deep ones are.

**Why.** The Metropolis chains are short and local, so they never move between
distant regions of c. The look is decided by where seeds come from (rule 5 in
[`METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md)). Uniform seeds are a fair proxy for walk seeds: both
take the first contributing point found, roughly uniform in area.

CPU, f64. Scripts: `seed_bias.py`, `seed_variants.py`, `seed_alpha.py`,
`seed_antenna.py` in [`experiments/`](experiments/), sharing `seedlib.py`. Run
with `uv run --with numpy python <script>` from that directory. "Antenna" is
c with Re < −1.4 and |Im| < 0.05; "cardioid side" is Re > −0.75. k is the
step of the first visit to the window.

| seeds | good | antenna | cardioid side | k < 10 / 10–39 / ≥ 40 | L p50 / p90 | log₁₀\|dz/dc\| p50 at first visit | solve steps per good seed |
|---|---|---|---|---|---|---|---|
| uniform (reference, 30M samples) | 126 | **81%** | 15% | 29 / 35 / 36% | 46 / 170 | **1.8** | ≈ 5M orbit steps (estimated, uniform) |
| A: Newton, boundary, n 10–40 (as committed) | 1259 / 20k | 3% | 69% | 0 / 100 / 0% | 38 / 43 | 4.4 | 9.3k |
| B: Newton, boundary, n 0–64 | 1449 / 20k | 3% | 72% | 2 / 41 / 57% | 45 / 62 | 4.8 | 10.4k |
| C: Newton, starts anywhere, n 0–64 | 86 / 20k | 47% | 38% | 43 / 29 / 28% | 43 / 66 | 3.4 | 175k |
| D: C + keep with p = min(1, (10/\|d\|)²) | 45 / 20k | 100% | 0% | 100 / 0 / 0% | 42 / 88 | 0.4 | 334k |
| boundary + keep with (10/\|d\|)^0.5 | 100 / 40k | 45% | 42% | 41 / 38 / 21% | 39 / 58 | 3.0 | 303k |
| boundary + keep with (10/\|d\|)^1 | 42 / 40k | 100% | 0% | 98 / 2 / 0% | 38 / 68 | 0.4 | 721k |
| boundary + 25% antenna starts, n 0–64 | 2591 / 40k | 18% | 60% | 4 / 47 / 48% | 45 / 62 | 4.7 | 11.6k |
| boundary + 50% antenna starts, n 0–64 | 2266 / 40k | 38% | 46% | 9 / 54 / 37% | 44 / 62 | 4.4 | 13.4k |
| boundary + 75% antenna starts, n 0–64 | 1972 / 40k | 62% | 28% | 14 / 62 / 24% | 43 / 62 | 4.0 | 15.3k |

Three biases, in order of effect:

1. **Start points.** Exclusion-boundary cells follow only the cardioid and the
   large bulbs. The antenna has no excluded interior beside it, so it never
   starts Newton. Yet 81% of uniform seeds for this window are on the
   antenna. Newton converges to roots near its start.
2. **n range.** n in [10, 40) excludes 65% of the reference: first visits
   before step 10 and after step 39.
3. **Derivative weighting.** Uniform sampling picks each preimage with
   probability ∝ 1/|dz_n/dc|² (the area of c that lands in the window).
   Newton favours large-|d| roots: median 10^4.4 against 10^1.8.
   Rejection with weight (D₀/|d|)^α is unusable: even α = 0.5 swings the
   distribution, and the cost rises 30–70×. Newton's basin sizes already
   cancel part of |d|² in a way that depends on the region, and a constant D₀
   is arbitrary.

**Antenna starts** fix most of the region bias at no cost, since solve steps
per seed barely change. But that is specific to windows whose good seeds are
on the antenna. Derivative and L-tail differences remain: Newton's L p90 is
62 against 170.

Tried on `feature/newton-seeding` with `SOLVE_MIN = 0`, `SOLVE_MAX = 64`
and `SOLVE_ANTENNA = 0.5`. The user's verdict in the explorer: it "works
insanely well", probably about 100× faster than walk seeding (by eye, not
measured). Committed (`c7c20ee`, in MR !3).

Measured by the user in the explorer at 1680 × 1189 (status lines):

| version | orbits | orbits/s | **points/s** | accepted |
|---|---|---|---|---|
| new, Newton with antenna starts (paused) | 1.58G | 52.3M | **509M** | 74% |
| old, random walk (running) | 173G | 5.25G | **4.08M** | 83.6% |

- **~125× more histogram points per second.** Points/s is the meaningful
  rate here. Orbits/s favours the walk because every seed candidate counts as
  an orbit, and walk candidates mostly end after a few steps without starting
  a chain.
- The image "fills almost instantly".
- **The look now resembles uniform seeding, less deep than the random walk.**
  The walk is not the same as uniform: it starts at the origin and resets
  every 256 steps, so it favours seeds near the middle of the set. To match
  the walk's depth, tune the start points (or the start map in
  [`IDEAS.md`](../IDEAS.md))
  against walk seeds rather than uniform ones. Alternatively, make the
  antenna share a "depth" setting.

**General options (not yet done):**

- **A start map learned per view and inherited when zooming** (written up in
  [`IDEAS.md`](../IDEAS.md), which also covers the starting-point database). A coarse map
  over c (Bitterli-style) that accumulates where *walk or uniform* seeds hit.
  When zooming in, the parent view's map is a good prior, because the
  sub-window's seeds are a subset of the parent's. Newton starts are drawn
  from it. It must not learn from Newton's own seeds, or the bias feeds back
  into the map.
- **Hybrid seeding:** walk first, Newton only after a lane's walk has failed
  for a while. This keeps the walk look wherever walk works. At deep zoom,
  Newton takes over, where walk draws nothing anyway.
- **Treat Newton as a look of its own:** a "deep structures" mode.
- **Longer, better-mixing chains** (occasional large mutations, as in Boswell's
  large steps) make the seed distribution matter less, at the cost of the
  current look's character.

## 2. Mutations scaled to the zoom

A mutation Δc moves orbit point k by about `d_k · Δc`. |d_k| is often in the
hundreds or more near ∂M, so a mutation of 1% of the view in c can move the
orbit by several views: the proposal misses the window, C = 0, and the
proposal is wasted. Options:

- **Derivative-scaled radius.** Record the step k at which the current state's
  orbit was in the window (or the largest |d_k| over in-window steps). Use
  radius ≈ `window_size · ρ / |d_k|`. This is symmetric only if the radius
  does not depend on the state; otherwise there is a Hastings factor.
- **Targeted jump.** `c' = c + (u − z_k)/d_k` with u uniform in the window:
  one Newton step toward a random pixel, so the chain spreads across the
  window. Asymmetric: the Hastings correction is about `|d_k(c')|² / |d_k(c)|²`
  (Jacobian of the map u ↦ c').
- Mix with the current isotropic mutation (Boswell-style small/large step mix),
  so the chain can still explore.
- The sampler is not meant to be unbiased
  ([`METROPOLIS_PLAN.md`](../plans/METROPOLIS_PLAN.md)), so the
  Hastings factor is a choice about the look, not about correctness.
- Watch: the accepted % in the status line and the debug line's
  `% drawn` (`escaped / orbits`).

## 3. Seed pool, warm restart, and the database

- **Seed pool.** A ring buffer in a storage buffer. A lane writes its state when
  it accepts a proposal with C > 0 (an atomic index; overwriting is fine). A
  lane that needs a new chain takes a pool entry with probability q and
  perturbs it, and otherwise seeds as usual (Newton). Keep the pool across
  view changes, and drop entries that no longer hit the window when they are
  next evaluated.
- **Warm restart.** Today every view change runs `begin`, discarding all
  chains, and clears the histogram. Instead, keep the chains and put every
  lane into a re-evaluation of its current state under the new parameters
  (`EVALUATE_SEED` from `current_r/i`). States that still hit the window keep
  going and the rest fall back to seeding. When zooming in, most states whose
  orbit crossed the new sub-window survive.
- **Database of starting points.** A seed is only useful for windows its orbit
  crosses, so a global DB needs indexing by orbit coverage and a backend to
  collect and serve seeds. With Newton seeding, the need mostly goes away.
  Cheaper versions:
  - save the pool per view in IndexedDB;
  - embed a few hundred seeds in the share URL or the exported PNG metadata,
    so shared views start warm;
  - keep a curated list of "places" as share URLs (see the URI settings).

## 4. Zoom UX

- **Placeholder image.** On a view change, keep the last displayed frame as a
  texture and draw it with the pan/zoom transform applied (as map viewers do)
  until the new histogram has enough samples. Then crossfade. Today every wheel
  event clears the histogram, so zooming flickers to black.
- **Lower resolution while moving.** During a gesture, sample into a ½ or ¼
  resolution histogram, which gives 4–16× more hits per pixel and a readable
  preview at once. Switch to full resolution after about 200 ms without input.
  `restart()` already reallocates on size change, so this can be a separate
  small histogram and display path.
- **Debounced restarts.** A wheel burst can call `restart()` on every event.
  Combined with warm restart and the placeholder, the image keeps building
  instead of starting over.
- **Precision gauge.** The c resolution needed is about `window / |d_k|`, finer
  than 1/scale. Show how much zoom is left before f32 breaks, from the
  measured |d_k| (for example, the median over recent accepted states)
  instead of the fixed `1/scale < 1e-6` warning.
- **Where to zoom: minibrot "ghosts" (hypothesis, untested).** The bright
  deep-iteration spots in Metropolis renders are probably near the cycle
  points of minibrots. A c just outside a period-p hyperbolic component escapes
  slowly and spends about L/p visits near each cycle point, so C is large there.
  - Precompute nuclei for small periods (mathr's nucleus method: Newton on
    `z_p(c) = 0` with `d`), their atom sizes, and cycle points.
  - Offer "jump to an interesting place".
  - Seed chains just outside the minibrot for windows containing its ghost.

  To check: with z₀ = c, the cycle of nucleus c₀ is `{c₀, c₀² + c₀, …, 0}`.
  The ghost is where the neutral cycle on the component's boundary sits, not
  the nucleus cycle, so the ghost is a curve swept as the internal angle
  varies.
- **Auto exposure.** With per-frame normalization (as in FracturingFog), a
  zoomed view does not look darker just because it has fewer hits.

## 5. Beyond f32 (deep zoom)

- **df64** (double-float emulation): GPU-portable, perhaps 4–8× slower per
  step. Needed for c and z once the window is near f32 spacing.
- **Perturbation.** Generally a poor fit for the Buddhabrot because contributing
  c are spread out. But Metropolis proposals are near the current state, so
  per-chain perturbation (a reference orbit for the current state in high
  precision, proposals as f32 deltas) is conceptually possible. Storage is the
  problem: up to 8192 points × 32k lanes × 8 bytes ≈ 2 GB. Fewer lanes, or
  re-deriving references, could help. A research item, not a next step.

## 6. GPU utilization (~50%)

Likely causes, roughly by expected impact:

1. **Gaps between dispatches.** `IN_FLIGHT = 2`, and each completion waits on
   a `mapAsync` readback that resolves late in the browser's event loop
   before the next submit.
   - Fixes: 3–4 in flight; accumulate counters on the GPU and read them back
     every N dispatches (or ring buffers of readbacks); several passes per
     command buffer.
   - Measure first: the debug line's "GPU orbits/s" (timestamp queries) against
     the status line's wall-clock orbits/s is the duty cycle.
2. **Fixed `THREADS = 32768`.** Tuned for the M5 Pro (16 cores × 2048).
   Scale it with the adapter, or tune it empirically.
3. **SIMD divergence.** Lanes in `REDRAW` and `EVALUATE_*` run different code
   in the same SIMD group, one path after the other. Make both use the same
   step path, with draw and count as conditions. Newton's `SOLVE` adds a third
   branch; keep its step code shared too (z update plus d update).
4. **Register pressure and occupancy.** The `Chain` struct is 80 bytes of
   private state and grows with Newton.
5. Atomic histogram contention at small windows (many lanes hitting the same
   pixels).

"Utilization" in Activity Monitor is mostly busy time, so (1) and (2) probably
explain most of the gap, and (3)–(5) are ALU efficiency on top.

## URI settings

Agreed design, implemented separately from this research (GitLab MR !2,
merged as `c506f3e`):

- Settings live in the URL hash and are updated with `history.replaceState`
  when a render starts. They are read on load before the full-view preset.
- Store the view as centre plus span in complex units (`min(w,h) / scale`), not
  pixels per unit, so a link shows the same region on any screen.
- Include sampler and display settings. Share one serializer with
  `exportMetadata()` (the PNG metadata). Loading settings from a dropped PNG
  is a natural follow-up.

## Plan

Item 1 is GitLab MR !3; the rest is open.

1. Newton seeding: `SOLVE` mode, boundary starting points, a `seeding`
   option. Compare `seeds/chains` and image quality against walk and uniform
   at several zooms (GPU runs by hand, when the GPU is free).
2. Seed pool and warm restart.
3. Placeholder image, lower resolution while moving, debounced restarts.
4. Derivative-scaled and targeted mutations.
5. GPU utilization fixes (measure the duty cycle first).
6. Minibrot list, df64, and a shared database once the rest is in.

## Appendix: experiment scripts

The scripts are in [`experiments/`](experiments/). The Python ones ran on the
CPU in f64 and need numpy; run them from that directory with
`uv run --with numpy python <script>`.

- [`seeds.py`](experiments/seeds.py): uniform against Newton seeding at four
  windows (the first table in section 1; about 30 s).
- [`diag.py`](experiments/diag.py): why Newton attempts fail, from random
  starts (the second table). Imports `evaluate` from `seeds.py`.
- [`diag2.py`](experiments/diag2.py): the same from boundary-pool starts.
  Imports from `seeds.py` and `diag.py`.
- [`seed_bias.py`](experiments/seed_bias.py),
  [`seed_variants.py`](experiments/seed_variants.py),
  [`seed_alpha.py`](experiments/seed_alpha.py),
  [`seed_antenna.py`](experiments/seed_antenna.py): the seed bias table
  ("The look changes"). The last three share
  [`seedlib.py`](experiments/seedlib.py); `seed_bias.py` is standalone.

The TypeScript ones use Deno's WebGPU and read
`web/src/shaders/metropolis.wgsl` and `data/exclusion.map` relative to the
repository; Deno fetches `fzstd` from npm. They expect the Newton seeding
shader (six bindings, 104-byte `Chain`), so they match MR !3, not the shader
before it.

- [`compile.ts`](experiments/compile.ts) (`deno run -A compile.ts`): compiles
  `metropolis.wgsl` and creates both pipelines with the six-binding layout.
  That catches WGSL errors without a browser.
- [`smoke.ts`](experiments/smoke.ts) (`deno run -A smoke.ts`): runs the
  sampler headless and prints counters per seeding mode, as in the GPU smoke
  test table. It runs on the GPU.
