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

## Research

- [`research/SEEDING_AND_ZOOM.md`](research/SEEDING_AND_ZOOM.md): seeding,
  mutations, zoom UX, deep zoom and GPU utilization for the explorer, with
  measurements. Its "Prior work" section collects the outside sources, with
  links: Boswell's Metropolis Buddhabrot, Bitterli's importance maps, mathr's
  Mandelbrot algorithms, and the equidistribution papers. Newton seeding
  (GitLab MR !3) came from it. Current as a reference; its plan items are open
  unless marked.
- [`research/experiments/`](research/experiments/): the scripts behind its
  numbers (Python with numpy, and Deno WebGPU). How to run each is in the
  research doc's appendix. Agents skip them unless the task is
  seeding or zoom research (`AGENTS.md`).

## History

- [`legacy/`](legacy/README.md): the original C renderer (2014), not built.
  Moved here from the repository root in GitLab MR !6.

Finished plans and session notes are not kept here: their open items moved to
`IDEAS.md` and `ISSUES.md`, and their decisions to the ADRs. They remain in
git history: the Metropolis, browser UI and Metal prototype plans, and the
September 2026 session notes. List them with
`git log --diff-filter=D --name-only -- docs/plans docs/notes`.
