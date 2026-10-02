# Ideas

Ideas worth doing but not yet planned. Each entry says where it came from and
links the research behind it. The detailed numbers and experiments are in
[`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md). Known problems
go in [`ISSUES.md`](ISSUES.md).

## Start map that follows the zoom (a database of starting points)

**Origin.** Two ideas turned out to be one:

- the October 2026 idea of "pre-computed starting points in a DB, updated over
  time";
- the Newton seeding finding that the image's look is decided by where chains
  start ("The look changes: seed bias" in the research doc).

The September 2026 session notes (in git history) had the same
direction ("importance sampling from a grid of starting-point cells"), and so
does Bitterli's importance map (see Prior work in the research doc).

**Problem.** Metropolis chains are short and local, so the image shows the
regions of c where seeds were found. Walk seeding finds seeds roughly in
proportion to area, which gives the look we want. But it stops finding any
past spans of about 1e-3. Newton seeding finds seeds at any depth, but from a
different distribution. At `#re=1.4&im=0&span=0.0148875`:

- 81% of walk-like seeds are on the antenna;
- 3% of Newton's seeds are, and 69% are near the main cardioid instead;
- Newton therefore draws the deep minibrots but not the faint one behind them.

**Idea.** Keep a coarse map over the c plane of where good seeds for the
current view come from, and start new seeds from it.

- **What the map holds.** Counts of good seeds (L > low, C > 0) per cell, over
  [−2, 0.5] × [−1.25, 1.25] or a sparse quadtree. It must learn only from
  samplers whose distribution is known: walk or uniform seeds, or the
  weighted samples below. If it learned from Newton's own seeds, Newton's bias
  would feed back into the map.
- **Inherited when zooming.** An orbit that visits a sub-window also visits the
  parent window, so the sub-window's good seeds are a subset of the parent's.
  The parent's map is a valid prior for the child: same support or larger,
  only coarser. When zooming in, start from the parent's map and refine it.
  When zooming out, merge or fall back to coarser maps.
- **Sampling from it without bias.** Draw c with the map as its probability
  density (pick a cell by its weight, then a uniform point inside the cell) and
  weight each result by 1/density. This is standard importance sampling, so
  what the map learns stays unbiased, and it can be refined level by level as
  the zoom deepens (adaptive importance sampling). This alone may be enough
  for moderate zooms, much more efficient than walk or uniform seeding.
- **Newton on top.** Where even map sampling finds too few seeds, start Newton
  from map cells instead of exclusion-boundary cells. That fixes the biggest
  of Newton's three biases (start points). The other two (n range, derivative
  weighting) are smaller, as measured in the research doc.
- **Becoming the database.**
  - The map is small: sparse non-empty cells with counts.
  - It can be saved per view in IndexedDB, and exported with a PNG or share
    link (by reference, if it is too large for the URL).
  - Merged across sessions, it becomes the "starting points updated over time"
    database: maps stored on a quadtree of view windows, each session adding
    its counts, every view inheriting from its ancestors.
  - It stores where seeds come from rather than individual seeds. So it is
    useful for every window under that tile, not only for one exact view.

**Open questions.**

- Resolution: fixed grid or adaptive quadtree, and how fine before cells
  are smaller than the mutation radius.
- What a view's map is keyed by: a quadtree tile of the z-plane window, and
  the channel limits (L > low depends on them).
- How quickly to forget a parent's prior once the child's own counts exist.
- Interaction with warm restart and the seed pool (sections 3 and 4 of the
  research doc). These overlap: the pool holds individual states, the map
  their distribution.
- Server side: storage, merging, and trust (a shared database accepts counts
  from anyone).

## Newton seeding with the random walk's depth

**Origin.** Trying Newton seeding (GitLab MR !3) in the explorer. With
antenna starts it gives about 125× more histogram points per second than
the walk, but its look is close to uniform seeding: less deep than the
random walk ("The look changes: seed bias" in the [research doc](research/SEEDING_AND_ZOOM.md)).

**Idea.** The walk starts at the origin and resets every 256 steps, so it
favours seeds near the middle of the set. Measure where walk seeds come from
(as `research/experiments/seed_bias.py` does for uniform seeds), then tune
Newton's start points to match. A cheaper first step is to make the antenna
share (`SOLVE_ANTENNA` in `metropolis.wgsl`) a setting, from less to more
deep. The start map above is the general version.

## Mutations scaled to the zoom

**Origin.** [research doc](research/SEEDING_AND_ZOOM.md), section 2.

**Idea.** A mutation Δc moves an orbit point by about |dz/dc|·Δc, and |dz/dc|
is often 10² to 10⁴. A mutation of 1% of the view therefore misses the window
when zoomed in. Scale the radius by the window size over |dz_k/dc|, or jump
straight to a random pixel with one Newton step (`c' = c + (u − z_k)/d_k`).
Mix with the current isotropic mutation so chains still explore.

## Seed pool and warm restart

**Origin.** [research doc](research/SEEDING_AND_ZOOM.md), section 3.

**Idea.**
- A shared ring buffer of accepted states that new chains start from, kept
  across view changes.
- On a pan or zoom, keep the chains and re-evaluate their states against the
  new window instead of discarding them. When zooming in, most states still
  hit the window.

Related to the start map: the pool holds states, the map their distribution.

## Smoother zooming

**Origin.** [research doc](research/SEEDING_AND_ZOOM.md), section 4.

**Idea.**
- Draw the previous frame transformed as a placeholder until the new render
  has enough samples, then crossfade.
- Sample at ½ or ¼ resolution while a gesture is in progress, and switch to
  full resolution once the view has been still for about 200 ms.
- A precision gauge from the measured |dz/dc|, replacing the fixed warning
  (see the span issue in [`ISSUES.md`](ISSUES.md)).
- "Jump to an interesting place" from a precomputed list of minibrot nuclei
  and their cycles. This is untested: the hypothesis is that bright
  deep-iteration spots sit near minibrot cycles.

## Deep zoom beyond f32

**Origin.** [research doc](research/SEEDING_AND_ZOOM.md), section 5.

**Idea.** Double-float emulation (df64) on the GPU for c and z. Per-chain
perturbation is conceptually possible, because Metropolis proposals stay near
the current state, but needs a reference orbit per lane (about 2 GB at 32k
lanes). This is the real fix for the precision artifacts in `ISSUES.md`.

## Metropolis lanes in the CLI renderers

**Origin.** The Metropolis plan (in git history), and GitLab MR !3, which
added Newton seeding to the explorer only
([ADR 0001](adr/0001-web-explorer-is-a-separate-product.md)).

**Idea.** The Metropolis lane (`chain` in `core/buddha_kernel.h`, GitHub
PR #17) runs only in the explorer's WGSL port so far:

- Give `buddha++` a `--sampler chain` and `buddha-metal` a Metropolis mode,
  both running the shared lane.
- Port Newton seeding (the `SOLVE` mode) to the kernel, so the CLI renders
  explorer views with the same seeding.
- Then retire the legacy sampler, with its known bugs (see
  [`ISSUES.md`](ISSUES.md)), and decide whether the lane takes over the
  `metropolis` name.

## Blending naive and Metropolis renders

**Origin.** The September 2026 session notes and the Metropolis plan's
Phase 3 (in git history).

**Idea.** Keep one histogram layer per sampler in the checkpoint and mix them
at TIFF time (for example `--mix`), with a mix setting in the explorer. The
alternatives, a flag that allows mixing in one histogram, or importance
weights that make Metropolis match naive, were judged weaker. Each extra
layer costs about 800 MB at 8192².

## Faster CPU histogram writes

**Origin.** The September 2026 session notes (in git history).

**Idea.** Histogram writes are about 40% of CPU render time at 8192². Batch
them per thread, or accumulate per-thread tiles. A faster scalar orbit loop
(GitHub PR #8, closed) made the kernel 2.4× faster but the whole generator
only 1.33×, because of these writes; revisit it only after them.

## A naive sampler in the explorer, for comparison

**Origin.** The user (October 2026). The explorer is Metropolis first because
naive sampling is useless when zoomed in
([ADR 0001](adr/0001-web-explorer-is-a-separate-product.md)), but a naive mode
would show what Metropolis changes and how much faster it is.
