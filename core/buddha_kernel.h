#ifndef BUDDHA_KERNEL_H
#define BUDDHA_KERNEL_H

// Rendering rules and the naive sampler, shared by the CPU renderer (C++) and buddha-metal
// (Metal Shading Language, compiled at run time from this file). Keeping one definition stops the
// two renderers from drifting apart; test/kernel_consistency.cpp and test/metal_consistency.mm
// check the parts that are not shared.
//
// The code is restricted to the common subset of C++ and MSL: no standard library in MSL builds,
// explicit address spaces through BUDDHA_THREAD, and plain uint/float structs for buffers.

#ifdef __METAL_VERSION__
#define BUDDHA_THREAD thread
#else
#include <cfloat>
#include <cmath>
#include <cstdint>
#define BUDDHA_THREAD
#endif

// Rounds every floating-point operation separately inside the function it opens, whatever the
// including code's flags (the project builds with -ffast-math). The CPU renderer, the shared lane
// on the CPU and the GPU then compute bit-identical results, and a lane's redraw pass follows
// exactly the orbit its first pass tested. render.metal sets the same for the whole GPU source.
#if defined(__clang__) && !defined(__METAL_VERSION__)
#define BUDDHA_EXACT _Pragma("clang fp contract(off) reassociate(off)")
#else
#define BUDDHA_EXACT
#endif

namespace buddha_kernel {

#ifndef __METAL_VERSION__
using uint = std::uint32_t;
using ulong = std::uint64_t;
using std::fabs;
#endif

// ---- rules ----

// An orbit has escaped once |z|^2 exceeds 8.
template <class T> inline bool escaped(T norm) { return norm > T(8); }

// Periodicity check: from step 8 on, z_i is compared with a checkpoint, which moves to steps 16,
// 32, 64, ... An orbit that returns to its checkpoint is periodic, so c is in the set.
template <class T> struct periodicity {
    T re, im;
    uint step;

    void reset() { step = 8; }

    bool cyclic(T zr, T zi, uint i) {
        BUDDHA_EXACT
        if (i == 8) {
            re = zr;
            im = zi;
        } else if (i > step) {
            const T dr = zr - re, di = zi - im;
            if (dr * dr + di * di < T(FLT_EPSILON) * T(FLT_EPSILON))
                return true;
            if (i == step * 2) {
                step *= 2;
                re = zr;
                im = zi;
            }
        }
        return false;
    }
};

// Cell of c in a size x size/2 exclusion map covering [-2, 2] x [-2, 0], mirrored.
template <class T>
inline void exclusion_cell(T cr, T ci, uint size, BUDDHA_THREAD int &x, BUDDHA_THREAD int &y) {
    BUDDHA_EXACT
    x = int(cr * T(size) / T(4) + T(size) / T(2));
    y = int(-fabs(ci) * T(size) / T(4) + T(size) / T(2));
}

inline bool inside_exclusion_map(int x, int y, uint size) {
    return x >= 0 && x < int(size) && y >= 0 && y < int(size / 2);
}

// Whether c is marked as inside the set. Map is anything indexable by cell (y * size + x).
template <class T, class Map>
inline bool excluded(T cr, T ci, uint size, BUDDHA_THREAD const Map &map) {
    int x, y;
    exclusion_cell(cr, ci, size, x, y);
    return inside_exclusion_map(x, y, size) && map[uint(y) * size + uint(x)] != 0;
}

// Histogram window. Windows centred on the real axis are symmetric: the lower half of the plane
// folds onto the upper one and the histogram covers only the upper half, including the middle
// row when the image height is odd.
template <class T> struct geometry {
    T minre, maxim, scale;
    uint width, height; // histogram size
    bool symmetric, odd_center;
};

template <class T>
inline bool pixel(BUDDHA_THREAD const geometry<T> &g, T re, T im, BUDDHA_THREAD uint &x,
                  BUDDHA_THREAD uint &y) {
    BUDDHA_EXACT
    const T image_x = (re - g.minre) * g.scale;
    if (!(image_x >= T(0) && image_x < T(g.width)))
        return false;
    const T imag = g.symmetric ? fabs(im) : im;
    const T image_y = (g.maxim - imag) * g.scale;
    if (!(image_y >= T(0) && image_y < T(g.height)))
        return false;
    x = uint(image_x);
    y = uint(image_y);
    return true;
}

// The unmirrored middle row of an odd-height symmetric image covers one strip; other rows
// accumulate both halves of the orbit. Weight it to match their expected density.
template <class T> inline uint pixel_weight(BUDDHA_THREAD const geometry<T> &g, uint y) {
    return g.odd_center && y + 1 == g.height ? 2 : 1;
}

// Whether step i of an orbit is drawn in a colour channel with iteration range (low, high).
inline bool in_channel(uint i, uint low, uint high) { return i > low && i < high; }

// Adds an orbit point to the enabled channels; Histogram provides add(x, y, channel, weight).
// Returns the number of increments, including the weight.
template <class T, class Histogram>
inline uint draw(BUDDHA_THREAD const geometry<T> &g, T re, T im, bool red, bool green, bool blue,
                 BUDDHA_THREAD Histogram &histogram) {
    uint x, y;
    if (!pixel(g, re, im, x, y))
        return 0;
    const uint weight = pixel_weight(g, y);
    uint increments = 0;
    if (red) {
        histogram.add(x, y, 0, weight);
        increments += weight;
    }
    if (green) {
        histogram.add(x, y, 1, weight);
        increments += weight;
    }
    if (blue) {
        histogram.add(x, y, 2, weight);
        increments += weight;
    }
    return increments;
}

// ---- naive sampler ----

// Kernel parameters, passed to Metal as a constant buffer: keep to uint and float fields.
struct parameters {
    uint offset;     // first sample index of this dispatch
    uint count;      // samples in this dispatch (at most 2^31)
    uint key0, key1; // random stream: sample n takes hash(2n ^ key0), hash(2n + 1 ^ key1)
    uint low, high;
    uint lowr, highr, lowg, highg, lowb, highb;
    uint width, histogram_height;
    uint symmetric, odd_center;
    uint exclusion_size;
    uint threads; // lanes sharing the samples of a dispatch
    float minre, maxim, scale;
    float padding;
};

struct totals {
    ulong iterations; // counted recurrence steps in the escape/periodicity pass
    ulong redraw;     // steps spent re-iterating escaping orbits to draw them
    ulong escaped, excluded, periodic;
    ulong increments; // histogram channel increments, including weights
};

inline uint mix(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

// 24-bit uniform values, exact in float.
inline float random01(uint value) {
    BUDDHA_EXACT
    return float(mix(value) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

// Starting point n of the stream (key0, key1), uniform over [-2, 2]^2.
inline void sample(BUDDHA_THREAD const parameters &p, uint n, BUDDHA_THREAD float &cr,
                   BUDDHA_THREAD float &ci) {
    BUDDHA_EXACT
    cr = random01((n * 2u) ^ p.key0) * 4.0f - 2.0f;
    ci = random01((n * 2u + 1u) ^ p.key1) * 4.0f - 2.0f;
}

template <class T> inline geometry<T> make_geometry(BUDDHA_THREAD const parameters &p) {
    geometry<T> g;
    g.minre = T(p.minre);
    g.maxim = T(p.maxim);
    g.scale = T(p.scale);
    g.width = p.width;
    g.height = p.histogram_height;
    g.symmetric = p.symmetric != 0u;
    g.odd_center = p.odd_center != 0u;
    return g;
}

// One lane of the naive sampler. Each call to advance() performs one orbit step, so lanes can run
// in lockstep: GPUs execute 32-thread SIMD groups together, and a lane whose orbit ends loads its
// next starting point in the same step instead of idling until the group's longest orbit ends.
//
// Pass 1 applies the escape and periodicity rules; orbits are not stored, so pass 2 re-iterates an
// escaping orbit to draw steps low..orbitMax, as the CPU renderer draws a stored orbit.
template <class T> struct lane {
    uint next, stride, end; // sample indices: next, next + stride, ... below end
    uint mode;              // 0: load a sample, 1: escape/periodicity pass, 2: redraw pass
    uint i, last;
    T cr, ci, zr, zi;
    periodicity<T> period;
    totals t;

    // z <- z^2 + c, given the squares of z.
    void step(T rr, T ii) {
        BUDDHA_EXACT
        const T next = rr - ii + cr;
        zi = T(2) * zr * zi + ci;
        zr = next;
    }

    void begin(uint first, uint lanes, uint count) {
        next = first;
        stride = lanes;
        end = count;
        mode = 0;
        t.iterations = t.redraw = t.escaped = t.excluded = t.periodic = t.increments = 0;
    }

    // Returns false once the lane has no samples left.
    template <class Map, class Histogram>
    bool advance(BUDDHA_THREAD const parameters &p, BUDDHA_THREAD const geometry<T> &g,
                 BUDDHA_THREAD const Map &map, BUDDHA_THREAD Histogram &histogram) {
        BUDDHA_EXACT
        if (mode == 0) {
            while (true) {
                if (next >= end)
                    return false;
                float r, im;
                sample(p, p.offset + next, r, im);
                next += stride;
                if (excluded(T(r), T(im), p.exclusion_size, map)) {
                    ++t.excluded;
                    continue;
                }
                cr = zr = T(r);
                ci = zi = T(im);
                i = 0;
                period.reset();
                mode = 1;
                break;
            }
        }

        const T rr = zr * zr, ii = zi * zi;
        if (mode == 1) {
            if (escaped(rr + ii)) {
                t.iterations += i;
                if (i == 0) { // orbitMax = -1: nothing to draw, as on the CPU
                    ++t.periodic;
                    mode = 0;
                    return true;
                }
                ++t.escaped;
                last = i - 1 < p.high - 1 ? i - 1 : p.high - 1;
                zr = cr;
                zi = ci;
                i = 0;
                mode = 2;
                return true;
            }
            if (period.cyclic(zr, zi, i)) {
                t.iterations += i;
                ++t.periodic;
                mode = 0;
                return true;
            }
            step(rr, ii);
            if (++i == p.high) {
                t.iterations += i;
                ++t.periodic;
                mode = 0;
            }
            return true;
        }

        if (i > last || escaped(rr + ii)) { // the second test only guards a diverged redraw
            t.redraw += i;
            mode = 0;
            return true;
        }
        if (i >= p.low)
            t.increments +=
                draw(g, zr, zi, in_channel(i, p.lowr, p.highr), in_channel(i, p.lowg, p.highg),
                     in_channel(i, p.lowb, p.highb), histogram);
        step(rr, ii);
        ++i;
        return true;
    }
};

} // namespace buddha_kernel

#endif
