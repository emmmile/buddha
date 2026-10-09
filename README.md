# Buddha++

<p align="center">
  <img src="assets/readme-header.avif" alt="Buddhabrot render" width="100%">
</p>

Buddha++ is a command-line, multi-threaded Buddhabrot renderer. It samples
complex orbits into a three-channel histogram, resumes long renders from Zstd
checkpoints, and writes a 16-bit TIFF image when the render stops.

The current renderer is deliberately headless. It is intended for long,
repeatable command-line jobs, including very large images on Apple Silicon.

## Features

- Multi-threaded Metropolis orbit sampling with configurable RGB iteration
  ranges.
- Independent randomized generator streams for each render session.
- Zstd-compressed checkpoints that can safely replace the loaded checkpoint
  when a render is resumed.
- 16-bit RGB TIFF output, including BigTIFF for images larger than 4 GiB.
- `--no-image` for frequent checkpoint-only saves during a long render.
- A Release build that enables LTO when the local toolchain supports it.

## Build

The project uses CMake 3.20 or newer, a C++23 compiler, Boost, libtiff, zlib,
Zstandard, and pthreads.
On macOS, install the dependencies with your preferred package manager, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting executable is `build/buddha++`.

## Commit checks

Install the repository's pre-commit hook once per clone:

```sh
git config core.hooksPath .githooks
```

The hook runs `git diff --cached --check` for whitespace errors, then checks
each kind of staged file with its own formatter. The historical `docs/legacy/`
directory is excluded.

- **C++, Objective-C++ and Metal:** `clang-format` checks only the changed
  lines against `.clang-format`.
- **Python** (including the hook itself): `ruff format --check` and
  `ruff check`, configured by `ruff.toml`. It uses `ruff` if installed, or
  `uvx ruff`. Fix with `uvx ruff format` and `uvx ruff check --fix`.
- **The explorer (`web/`):** Prettier, ESLint and the TypeScript compiler, using
  the tools installed in `web/node_modules` (see
  [Browser explorer](#browser-explorer)). Fix with `yarn format` and
  `yarn lint` in `web/`.
- **TypeScript and JavaScript elsewhere** (such as the Deno scripts in
  `docs/`): the explorer's Prettier with `web/.prettierrc`.

All formatters use 100 columns. Stage the fixed files again after fixing.

CI runs in two places:

- **GitLab** (`.gitlab-ci.yml`), on merge requests and the default branch:
  - a `web` job formats, lints, type checks and builds the explorer, and checks
    TypeScript under `docs/` with its Prettier;
  - a `lint` job runs the hook's whitespace, clang-format and ruff checks on
    every change since the base commit.

  Its free runners are Linux, and their images lack Boost 1.88, so it does not
  build the native code. clang-format 21.1.8 from PyPI matches Xcode's
  clang-format 21 on this repository.
- **GitHub Actions** (`.github/workflows/ci.yml`):
  - an Ubuntu job checks and builds the explorer;
  - a macOS job runs the same hook checks, then builds and runs the native
    tests.

## Run

Start a render with explicit geometry, scale, and output stem:

```sh
./build/buddha++ \
  --width 8192 --height 8192 --scale 2048 \
  --threads 10 --out render
```

Press `Ctrl-C` to stop the generators and save `render.zst`; unless
`--no-image` was supplied, it also writes `render.tiff`. Use `--help` for all
options.

To resume a checkpoint, use the same image geometry and rendering parameters:

```sh
./build/buddha++ \
  --load render.zst \
  --width 8192 --height 8192 --scale 2048 \
  --threads 10
```

When `--out` is omitted on a resumed render, the checkpoint stem is reused and
the completed checkpoint atomically replaces the previous one. Supplying a
different `--out` creates a new checkpoint instead.

New checkpoints record image geometry, iteration ranges and the sampler
(`--sampler metropolis`, the default, or `naive`) and refuse to load with
different settings. Checkpoints written before the sampler was recorded load
as Metropolis. The oldest checkpoints lack this metadata entirely. After
verifying their settings yourself, pass `--allow-legacy-checkpoint` once to
load and rewrite one in the new format.
Legacy checkpoints can only be imported for even-height windows centered on
the real axis, which retain the old histogram layout.

Windows centered on the real axis use a mirrored histogram to save memory.
An off-axis window (`--cim` other than zero) uses a full-height histogram.

### Exclusion map

Samples that fall inside the Mandelbrot set never escape, so both renderers
skip them with an exclusion map. `data/exclusion.map` is an 8192² map
computed with 65536 iterations. Every binary and test loads it by default;
`--exclusion-map` (`-e`) selects another one. Each file stores its own
resolution and iteration count, so no size option is needed. A warning is
logged when the render uses more iterations than the map was computed with.

`./build/exclusion -e new.map` generates an 8192² map with the given
iterations (`-R`), then refines it until `Ctrl-C` saves it. Given an
existing map, it refines that map instead.

## GPU rendering on Apple silicon (experimental)

On macOS the build also produces `buddha-metal`, a Metal renderer that takes
the same options and writes the same checkpoints and TIFF images:

```sh
./build/buddha-metal --width 8192 --height 8192 --scale 2048 --out render
```

It uses a different sampler than `buddha++`'s default: starting points are
sampled uniformly over `[-2, 2]²` (naive Buddhabrot) in single precision,
without the Metropolis chains. `buddha++ --sampler naive` runs the same
sampler on the CPU and, given the same random stream, produces the same
histogram. The rendering rules live in
`core/buddha_kernel.h`, shared by both renderers and checked by
`kernel-consistency` and `metal-consistency`. At 8192² it fills the histogram
about four times faster than the CPU renderer on an M5 Pro, but the images
look different, and naive sampling wastes most samples on zoomed-in views.
Checkpoints record the sampler, so a Metropolis checkpoint cannot be continued
by `buddha-metal`, nor a naive one by `buddha++` without `--sampler naive`.
See [metal/README.md](metal/README.md) for details and benchmarks.

### Browser explorer

`web/` holds an interactive explorer that runs entirely in the browser on
WebGPU, with no server. Use it to find a region, then render that region at
full size with `buddha++`. It is plain TypeScript, without a UI framework,
built with Vite. It needs Node 22 and Yarn 4, which corepack provides:

```sh
cd web
corepack enable
yarn install
yarn dev      # development server with live reload
yarn build    # static site in web/dist, publishable as is
yarn check    # formatting, lint and type check
yarn test     # settings and GPU/session regression checks
```

Open it in a browser with WebGPU, such as Chrome, Edge or Safari 26.

The explorer runs Metropolis chains (`web/src/shaders/metropolis.wgsl`, a WGSL port of
`chain` in `core/buddha_kernel.h`) or, for comparison, a naive sampler (uniform
samples over `[-2, 2]²`, each escaping one drawn once: the unbiased
Buddhabrot, which finds little when zoomed in) into a histogram of one bin per device
pixel of the canvas, scaled down only on screens larger than the GPU's buffer
limits allow. Each orbit point is also drawn at its mirror image across the
real axis wherever that adds points, which gives up to twice as many: always
with the naive sampler, and with Metropolis when the view crosses the axis.
The window is its centre and span, the complex units across the
view's shorter side, so resizing the browser keeps the same region. Drag to
pan, and scroll or pinch to zoom around the pointer. The image follows the
gesture. Display adjustments and the main actions stay visible; coordinates,
channel ranges, sampler tuning and render diagnostics live in collapsible sections.
Changing the window, channel iterations or Metropolis settings starts
a new histogram, as does a resize once it settles. Exposure (in stops)
and gamma set the tone curve, the same one `buddha++` applies. Saturation, texture
(midtone detail a few pixels across) and sharpness (a pixel-scale unsharp mask)
adjust the preview on the GPU. None of the display controls restart sampling.
Pause stays in effect when navigating, changing settings, resizing or resetting.
*Reset view* restores the full window while retaining sampler and display settings;
*Reset all* restores all settings. GPU actions are disabled until startup finishes;
if startup, the GPU device or a sampler readback fails, *Retry GPU* starts a fresh
render with the committed settings and the same pause state.
The measures at the bottom of the settings panel show the histogram points and
orbits so far and their rates, counted as the Metal sampler counts them, so
the numbers compare directly with `metal/`. Then come the acceptance rate,
the share of orbits drawn, how busy the GPU is, and the image size.

*Export* saves the image on screen at display resolution, as lossless WebP,
encoded in a worker by libwebp compiled to WebAssembly (as `cwebp -z 3 -exact`,
11-23% smaller than browsers' own WebP, and available in Safari too). The WebP
is decoded again and kept only if every pixel matches; otherwise, or if the
encoder fails to load, the export is a PNG. The view, sampler and display settings, and the orbit and point
counts, are embedded as XMP in a WebP and as text chunks in a PNG: `exiftool
image.webp` lists them, and the `buddha-explorer` entry holds them as JSON. Export uses the sampler settings of the current histogram and the
current display adjustments, so unfinished or invalid form edits cannot describe a
different view in the saved image.

Differences from the native kernel: orbits are single precision, like
`buddha-metal`, so the page warns past that limit, and the histogram is not
mirrored about the real axis. Starting points are rejected with the same
exclusion map (`data/exclusion.map`, bundled into the build).

## Documentation

Plans, research notes, known issues and design decisions are in `docs/`; see
[docs/README.md](docs/README.md) for an index.

## Historical Qt GUI

The original interactive Qt navigator is preserved on the
[`legacy-qt-gui`](https://github.com/emmmile/buddha/tree/legacy-qt-gui) branch,
anchored at commit
[`4996093`](https://github.com/emmmile/buddha/tree/4996093a5bea1caf35c517cd50c55e5d8c1cd376).
It remains a historical reference and is not built by this command-line
renderer.

## Future work

The browser explorer replaces the Qt GUI for navigation. Long renders stay on
the command line. Possible next steps are exporting the explorer's settings to
a file that `buddha++` can load, and bringing the Metropolis settings to the
command line so a full render matches its preview.
