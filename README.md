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
- Deterministic runs with `--seed`, or randomized seeds by default.
- Zstd-compressed checkpoints that can safely replace the loaded checkpoint
  when a render is resumed.
- 16-bit RGB TIFF output, including BigTIFF for images larger than 4 GiB.
- `--no-image` for frequent checkpoint-only saves during a long render.
- A Release build that enables LTO when the local toolchain supports it.

## Build

The project uses CMake, Boost, libtiff, zlib, Zstandard, and pthreads.
On macOS, install the dependencies with your preferred package manager, then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting executable is `build/buddha++`.

## Run

Start a render with explicit geometry, scale, output stem, and seed:

```sh
./build/buddha++ \
  --width 8192 --height 8192 --scale 2048 \
  --threads 10 --seed 42 --out render
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

The separate `prototype/metal-orbit-benchmark` branch contains only an
Apple-silicon CPU-versus-Metal orbit-loop benchmark. It is intentionally not
part of this renderer or its modernization pull request.
