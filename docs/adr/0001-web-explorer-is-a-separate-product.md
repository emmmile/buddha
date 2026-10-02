# 0001. The web explorer is a separate product from the Metal/CLI renderers

**Status:** Accepted
**Date:** 2026-10-01

## Context

The first interactive UI was `buddha-browser` (GitHub PRs #13 to #17,
[`plans/BROWSER_UI_PLAN.md`](../plans/BROWSER_UI_PLAN.md)): a native macOS
process that rendered with Metal, served a page on loopback, and sent RGBA
frames over HTTP. The display adjustments ran natively so that a TIFF export
could match the preview. It needed a running native server, and every sampler
had to be wired through its session, worker and HTTP layers.

Commit `9b9a0ba` (GitLab MR !1, merged as `0bd8588`) replaced it with a
WebGPU explorer in `web/`: Metropolis chains, histogram and display all run in
the browser, with no server. `buddha-browser`, its HTTP server and its tests
were removed; `buddha++` and `buddha-metal` stayed the command-line renderers.
The commit message and README describe what changed, not why. The reasons
below were confirmed by the user (October 2026).

## Decision

The explorer is its own product, not a front end to the native renderers.

- **Client side, so it can be published.** The main reason for the switch: the
  explorer is meant to be published eventually, so everything has to run in
  the visitor's browser, with no server doing the rendering. A native server
  could only ever be a local tool.
- **Metropolis first.** The explorer is about zooming, and naive sampling is
  useless when zoomed in: almost no samples land in the window. A naive
  sampler may be added later for comparison.
- **Self-contained.** It runs entirely in the browser on WebGPU and builds to
  a static site (`yarn build`, "publishable as is" in the README).
- **Purpose: exploration and sharing.** Find a region, then share it as a link
  (all settings in the URL hash, MR !2) or a PNG with its settings in text
  chunks (`buddha-explorer/1` JSON, `0607ba4`). Full-size, high-quality renders
  stay with `buddha++` and `buddha-metal` ("find a region here, export it, then
  render it at full size with the buddha++ command line", `web/src/main.ts`).
- **Its own sampler code.** `web/src/shaders/metropolis.wgsl` is a port of
  `chain` in `core/buddha_kernel.h`, not shared code. The two may diverge, and
  there is no bit-identical guarantee between them (unlike
  [ADR 0003](0003-shared-bit-identical-kernel.md)).

A side effect, not a stated reason: WebGPU also runs beyond Apple hardware
(Chrome, Edge, Safari), unlike the Metal-only native server.

## Consequences

- Kernel logic is duplicated in WGSL. A fix to the shared kernel does not
  reach the explorer by itself, and no test compares them.
- Features can land in the explorer first. Newton seeding (MR !3) exists only
  there; it is not in `core/buddha_kernel.h` or `buddha++`.
- A view found in the explorer does not render identically on the CLI today:
  `buddha++`'s default Metropolis sampler is still the legacy one, the
  explorer's sampler settings have no CLI equivalent, and saturation, texture
  and sharpness exist only in the explorer (the tone curve, exposure and
  gamma, matches `buddha++`). The goal is parity between the two; see
  "Display adjustments differ between the explorer and the CLI" in
  [`ISSUES.md`](../ISSUES.md). The README's "Future work" names the fix: a settings file that
  `buddha++` can load, and Metropolis settings on the command line.
- So the hand-off format matters: the URL hash and PNG metadata should stay
  stable and close to the CLI's options (centre, channel ranges, sampler
  settings), and changes to either side should keep them loadable.
- The web UI can change quickly without touching the native build, its
  checkpoint format or its tests. CI checks it in a separate job.
