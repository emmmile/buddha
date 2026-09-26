#ifndef BUDDHA_METAL_RENDER_KERNEL_H
#define BUDDHA_METAL_RENDER_KERNEL_H

#include <cstdint>

// Naive (uniform sampling) Buddhabrot kernel shared by buddha-metal and metal-render-benchmark.
// The Metal source below mirrors these structs; keep both layouts in sync.

namespace buddha_metal {

constexpr uint32_t group_size = 256;

struct parameters {
    uint32_t offset;     // first sample index of this dispatch
    uint32_t count;      // samples in this dispatch (at most 2^31)
    uint32_t key0, key1; // per-stream random key: sample n uses hash(2n ^ key0), hash(2n+1 ^ key1)
    uint32_t low, high;
    uint32_t lowr, highr, lowg, highg, lowb, highb;
    uint32_t width, histogram_height;
    uint32_t symmetric, odd_center;
    uint32_t exclusion_size;
    uint32_t threads; // persistent kernel: threads in this dispatch
    float minre, maxim, scale;
    float padding;
};

struct thread_totals {
    uint64_t iterations; // counted recurrence steps in the escape/periodicity pass
    uint64_t redraw;     // steps spent re-iterating escaping orbits to draw them
    uint64_t escaped, excluded, periodic;
    uint64_t increments; // histogram channel increments, including symmetry weights
};

inline uint32_t mix(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

// 24-bit uniform values are exact in float, so CPU mirrors can start from the same c.
inline float random01(uint32_t value) {
    return static_cast<float>(mix(value) & 0x00ffffffU) * (1.0f / 16777216.0f);
}

inline void sample_point(const parameters &p, uint32_t n, float &cr, float &ci) {
    cr = random01((n * 2U) ^ p.key0) * 4.0f - 2.0f;
    ci = random01((n * 2U + 1U) ^ p.key1) * 4.0f - 2.0f;
}

inline const char *kernel_source = R"metal(
#include <metal_stdlib>
using namespace metal;

struct Parameters {
    uint offset;
    uint count;
    uint key0, key1;
    uint low, high;
    uint lowr, highr, lowg, highg, lowb, highb;
    uint width, histogram_height;
    uint symmetric, odd_center;
    uint exclusion_size;
    uint threads;
    float minre, maxim, scale;
    float padding;
};

struct ThreadTotals {
    ulong iterations, redraw, escaped, excluded, periodic, increments;
};

uint mix(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float random01(uint value) {
    return float(mix(value) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float2 sample_point(constant Parameters &p, uint n) {
    return float2(random01((n * 2u) ^ p.key0) * 4.0f - 2.0f,
                  random01((n * 2u + 1u) ^ p.key1) * 4.0f - 2.0f);
}

// Same lookup as mandelbrot::excluded, in float.
bool excluded(constant Parameters &p, device const uchar *map, float2 c) {
    const float size = float(p.exclusion_size);
    const int x = int(c.x * size / 4.0f + size / 2.0f);
    const int y = int(-fabs(c.y) * size / 4.0f + size / 2.0f);
    return x >= 0 && x < int(p.exclusion_size) && y >= 0 && y < int(p.exclusion_size / 2u) &&
           map[uint(y) * p.exclusion_size + uint(x)] != 0;
}

// Same mapping, bounds and odd-height centre-row weight as buddha_generator::drawPoint.
void draw(constant Parameters &p, device atomic_uint *raw, float zr, float zi, uint i,
          thread uint &increments) {
    const float image_x = (zr - p.minre) * p.scale;
    if (!(image_x >= 0.0f && image_x < float(p.width)))
        return;
    const float imag = p.symmetric ? fabs(zi) : zi;
    const float image_y = (p.maxim - imag) * p.scale;
    if (!(image_y >= 0.0f && image_y < float(p.histogram_height)))
        return;
    const uint x = uint(image_x), y = uint(image_y);
    const uint index = (y * p.width + x) * 3u;
    const uint weight = (p.odd_center != 0u && y + 1u == p.histogram_height) ? 2u : 1u;
    if (i < p.highr && i > p.lowr) {
        atomic_fetch_add_explicit(&raw[index], weight, memory_order_relaxed);
        increments += weight;
    }
    if (i < p.highg && i > p.lowg) {
        atomic_fetch_add_explicit(&raw[index + 1u], weight, memory_order_relaxed);
        increments += weight;
    }
    if (i < p.highb && i > p.lowb) {
        atomic_fetch_add_explicit(&raw[index + 2u], weight, memory_order_relaxed);
        increments += weight;
    }
}

// Persistent threads. A GPU runs 32-thread SIMD groups in lockstep, so one sample per thread
// makes each group wait for its longest orbit. Here each thread walks many samples as a state
// machine advancing one orbit step per loop iteration: a lane whose orbit escapes or turns
// periodic loads its next sample in the same iteration. Orbits are not stored, so escaping ones
// are re-iterated (mode 2) to draw points low..orbitMax, as the CPU renderer draws them.
kernel void render_persistent(
    constant Parameters &p [[buffer(0)]],
    device const uchar *map [[buffer(1)]],
    device atomic_uint *raw [[buffer(2)]],
    device ThreadTotals *totals [[buffer(3)]],
    uint tid [[thread_position_in_grid]]) {
    if (tid >= p.threads)
        return;

    const float epsilon2 = FLT_EPSILON * FLT_EPSILON;
    ulong iterations = 0, redraw = 0, escaped = 0, excluded_count = 0, periodic = 0;
    uint increments = 0u;

    uint k = tid;   // next sample, strided by the dispatch's thread count
    uint mode = 0u; // 0: load a sample, 1: escape/periodicity pass, 2: redraw pass
    float2 c = float2(0.0f);
    float zr = 0.0f, zi = 0.0f, pr = 0.0f, pi = 0.0f;
    uint i = 0u, criticalStep = 8u, last = 0u;

    while (true) {
        if (mode == 0u) {
            while (k < p.count) {
                c = sample_point(p, p.offset + k);
                k += p.threads;
                if (excluded(p, map, c)) {
                    ++excluded_count;
                    continue;
                }
                zr = c.x;
                zi = c.y;
                i = 0u;
                criticalStep = 8u;
                mode = 1u;
                break;
            }
            if (mode == 0u)
                break; // no samples left
        }

        const float rr = zr * zr, ii = zi * zi;
        if (mode == 1u) {
            // Mirrors mandelbrot_base::evaluate and cyclic: escape, then periodicity.
            if (rr + ii > 8.0f) {
                iterations += i;
                ++escaped;
                last = min(i - 1u, p.high - 1u); // orbitMax = i - 1, and i >= 1 here
                zr = c.x;
                zi = c.y;
                i = 0u;
                mode = 2u;
                continue;
            }
            if (i == 8u) {
                pr = zr;
                pi = zi;
            } else if (i > criticalStep) {
                const float dr = zr - pr, di = zi - pi;
                if (dr * dr + di * di < epsilon2) {
                    iterations += i;
                    ++periodic;
                    mode = 0u;
                    continue;
                }
                if (i == criticalStep * 2u) {
                    criticalStep *= 2u;
                    pr = zr;
                    pi = zi;
                }
            }
            const float next = rr - ii + c.x;
            zi = 2.0f * zr * zi + c.y;
            zr = next;
            if (++i == p.high) {
                iterations += i;
                ++periodic;
                mode = 0u;
            }
        } else {
            if (i > last || rr + ii > 8.0f) {
                redraw += i;
                mode = 0u;
                continue;
            }
            if (i >= p.low)
                draw(p, raw, zr, zi, i, increments);
            const float next = rr - ii + c.x;
            zi = 2.0f * zr * zi + c.y;
            zr = next;
            ++i;
        }
    }

    // Each thread owns its slot across dispatches, which run in order on one queue.
    totals[tid].iterations += iterations;
    totals[tid].redraw += redraw;
    totals[tid].escaped += escaped;
    totals[tid].excluded += excluded_count;
    totals[tid].periodic += periodic;
    totals[tid].increments += increments;
}
)metal";

} // namespace buddha_metal

#endif
