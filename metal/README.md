# Metal orbit-only benchmark

This is deliberately not a renderer.  It compares the CPU and Metal versions
of the same naive, single-precision `z = z * z + c` orbit loop.  Neither path
uses a histogram, checkpoint, image buffer, or image encoder.

The GPU writes one pair of counters per threadgroup, rather than one result per
orbit.  That tiny buffer prevents the optimizer from removing the orbit loop
without measuring global histogram atomic contention.

Configure and build with:

```sh
cmake -S . -B build-metal -DCMAKE_BUILD_TYPE=Release
cmake --build build-metal --target metal-orbit-benchmark --parallel
./build-metal/metal-orbit-benchmark --orbits 1000000 --iterations 1024
```

The executable accepts `--orbits`, `--iterations`, `--seed`, and `--threads`.
The CPU and GPU totals are diagnostic checks only: small floating-point
differences can alter escape trajectories, so throughput is the comparison.

Each run also reports memory in MiB as **before / sampled peak / after**.
Resident is process memory held in physical RAM; footprint is macOS's physical
memory accounting for the process. The Metal line additionally shows
`MTLDevice.currentAllocatedSize`, the size of Metal resources allocated by
this process. These values overlap on unified-memory Macs and must not be
added together. The Metal interval includes shader compilation, buffer
allocation, warm-up, and the measured dispatch. CPU and Metal run in the same
process, so the Metal baseline includes memory retained by the CPU run.

The process measurements are sampled every 5 ms plus at interval boundaries;
short-lived peaks may be missed. Sampling adds a small amount of CPU work, so
throughput is best compared across repeated runs with the same settings. The
benchmark needs a Metal-capable device at runtime; Xcode's Metal tools are not
needed because the kernel is compiled from source by the Metal API.
