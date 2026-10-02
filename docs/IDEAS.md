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

The [September session notes](notes/SESSION_NOTES_2026-09.md) had the same
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
