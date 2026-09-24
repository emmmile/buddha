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
