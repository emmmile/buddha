# Documentation

What each document is for, and whether it describes the code as it is
(current), is kept up to date as work goes on (living), or records how things
got here (history). Start with the top-level [README](../README.md) for
building and running, and [`metal/README.md`](../metal/README.md) for the GPU
renderer and the shared kernel.

## Living

- [`IDEAS.md`](IDEAS.md): ideas worth doing but not yet planned, with where
  each came from.
- [`ISSUES.md`](ISSUES.md): known problems, what is known about each, and a
  proposed fix.
- [`VIEWS.md`](VIEWS.md): reference views in the explorer for debugging and
  comparing changes, as URL hashes.

These are files rather than GitLab or GitHub issues
([ADR 0005](adr/0005-ideas-and-issues-as-files.md)).

## Decisions

- [`adr/`](adr/README.md): Architecture Decision Records, with the template
  and an index. Current.

## Plans

- [`plans/METROPOLIS_PLAN.md`](plans/METROPOLIS_PLAN.md): moving Metropolis to
  GPU lanes while keeping its look. Partly done; a status note at the top says
  what shipped.
- [`plans/BROWSER_UI_PLAN.md`](plans/BROWSER_UI_PLAN.md): the native
  `buddha-browser` UI. Superseded by the WebGPU explorer in `web/`.
- [`plans/METAL_PROTOTYPE_PLAN.md`](plans/METAL_PROTOTYPE_PLAN.md): whether the
  GPU can speed up the histogram, and its outcome (`buddha-metal`). History;
  its "Metropolis set aside" outcome was later reversed by GitHub PR #17.

## Research

- [`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md): seeding,
  mutations, zoom UX, deep zoom and GPU utilization for the explorer, with
  measurements. Newton seeding (GitLab MR !3) came from it. Current as a
  reference; its plan items are open unless marked.
- [`research/experiments/`](research/experiments/): the scripts behind its
  numbers (Python with numpy, and Deno WebGPU). How to run each is in the
  research doc's appendix.

## Notes

- [`notes/SESSION_NOTES_2026-09.md`](notes/SESSION_NOTES_2026-09.md): handover
  from the September 2026 CPU and Metal work (PRs #8 to #11). History.
