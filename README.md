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

The hook runs `git diff --cached --check` for whitespace errors and requires
`clang-format` for staged C++ files. It checks only changed C++ lines against
`.clang-format`. The historical `legacy/` directory is excluded. Format any
reported lines and stage them again before committing.

CI (`.github/workflows/ci.yml`) runs the same hook on every change since the
base commit, then builds on macOS and runs the tests.

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

### Local browser prototype

On macOS, build and launch the separate interactive prototype with:

```sh
cmake --build build --target buddha-browser --parallel
./build/buddha-browser
```

It serves a page on `127.0.0.1` and opens it in the default browser. The
viewport determines the render resolution, capped at two million pixels. The
current Metal sampler renders overview and shallow-zoom views; a raw RGBA
preview updates while it runs. Start or Stop from the page, and press `Ctrl-C` in the
terminal to exit. Use `--no-open` to print the URL without opening a browser.
The browser renderer loads the committed `data/exclusion.map` automatically.
Drag the image to pan; scroll or pinch to zoom around the pointer. Resizing the
window starts a new display-sized render after a short pause.
Brightness, contrast, saturation, clarity, and texture update the
preview from the current histogram, including after Stop, without restarting
the sampler. Brightness lifts midtones while preserving black and white;
clarity affects broad midtone contrast, while texture affects fine detail.
The headless `buddha++` and `buddha-metal` binaries remain available for large
renders and automation.

## Historical Qt GUI

The original interactive Qt navigator is preserved on the
[`legacy-qt-gui`](https://github.com/emmmile/buddha/tree/legacy-qt-gui) branch,
anchored at commit
[`4996093`](https://github.com/emmmile/buddha/tree/4996093a5bea1caf35c517cd50c55e5d8c1cd376).
It remains a historical reference and is not built by this command-line
renderer.

## Future work

The preferred successor to the Qt GUI is a browser-based interface: interactive
navigation and render controls in the browser, with progressive previews and
checkpoint-aware long-running jobs. A WebGPU/WebAssembly implementation would
also make GPU experimentation portable while retaining the reproducible
renderer configuration described above.
