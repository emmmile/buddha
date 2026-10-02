# Architecture Decision Records

An ADR records one decision that shapes the code: the situation, what was
decided, and what follows from it. They are short and written once. When a
decision changes, write a new ADR that supersedes the old one and update the
old one's status; do not rewrite history.

Write one when a choice would surprise a newcomer, constrains later work, or
was argued over. Cite the commit, merge request or doc the rationale comes
from. Where the rationale is not written down anywhere, say so ("to confirm
with the user") rather than inventing one.

Files are numbered in order: `NNNN-short-title.md`.

## Template

```markdown
# NNNN. Title in the imperative or as a statement

**Status:** Proposed | Accepted | Superseded by NNNN
**Date:** YYYY-MM-DD

## Context

What forced the decision: the problem, constraints, and options considered.

## Decision

What was decided, in a few sentences.

## Consequences

What becomes easier or harder, and what has to be kept in step.
```

## Index

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-web-explorer-is-a-separate-product.md) | The web explorer is a separate product from the Metal/CLI renderers | Accepted |
| [0002](0002-metropolis-optimises-for-the-look.md) | The Metropolis sampler optimises for the look, not an unbiased estimate | Accepted |
| [0003](0003-shared-bit-identical-kernel.md) | CPU and Metal share `core/buddha_kernel.h` and stay bit-identical | Accepted |
| [0004](0004-gpu-samplers-as-persistent-lanes.md) | GPU samplers are persistent lanes that advance one orbit step per call | Accepted |
| [0005](0005-ideas-and-issues-as-files.md) | Ideas and known issues live as files in `docs/`, not in an issue tracker | Accepted |

## Candidates

Decisions that may deserve an ADR, but whose rationale is not (yet) written
down well enough to record one:

- **Display adjustments belong to the preview, not the image.** The native
  browser plan made them part of the image, applied to TIFF export through a
  shared native pipeline (`plans/BROWSER_UI_PLAN.md`, `core/image_pipeline.h`).
  The WebGPU explorer moved saturation, texture and sharpness to the GPU and
  `e95748b` removed the native pipeline, so the CLI now applies only the tone
  curve (exposure and gamma). Whether that is the intended long-term split is
  to confirm with the user.
- **Checkpoints record the sampler and refuse a mismatch** (README, session
  notes, PR #9). Related: the preferred, not yet built, design for blending
  naive and Metropolis renders as separate histogram layers mixed at TIFF time
  (`--mix`, session notes and `plans/METROPOLIS_PLAN.md` Phase 3).
- **Single precision on the GPU, and a span limit in the explorer** until
  df64 or perturbation exists (`ISSUES.md`, research doc section 5). The limit
  is proposed, not decided.
- **The explorer is plain TypeScript with Vite and no UI framework** (README,
  `9b9a0ba`). No rationale is written down.
- **Explorer links store the view as centre and span**, not pixels per unit,
  so a link shows the same region on any screen (`6aafd7a`, research doc "URI
  settings"). Small; may not need its own record.
