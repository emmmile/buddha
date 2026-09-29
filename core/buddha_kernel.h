#ifndef BUDDHA_KERNEL_H
#define BUDDHA_KERNEL_H

// Rendering rules and the naive and Metropolis samplers, shared by the CPU renderer (C++) and
// buddha-metal (Metal Shading Language, compiled at run time from this file). Keeping one
// definition stops the two renderers from drifting apart; test/kernel_consistency.cpp and
// test/metal_consistency.mm check the parts that are not shared.
//
// The code is restricted to the common subset of C++ and MSL: no standard library in MSL builds,
// explicit address spaces through BUDDHA_THREAD, and plain uint/float structs for buffers.

#ifdef __METAL_VERSION__
#define BUDDHA_THREAD thread
#else
#include <bit>
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
    // Metropolis sampler only.
    ulong proposals, accepted;
    ulong seeds;  // seed candidates tried
    ulong chains; // chains started
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
        t.proposals = t.accepted = t.seeds = t.chains = 0;
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

// ---- Metropolis sampler ----
//
// A Metropolis-Hastings chain over starting points c, with target density L^a * C^b, where L is
// the orbit's last index before escape and C the number of orbit points inside the window. Every
// valid proposal is drawn once, accepted or not, and nothing reweights the samples: the image is
// not the Buddhabrot but a view biased toward long orbits that stay in the window. The defaults
// (a = 2, b = 1, chains of max(256 C, 2 L) proposals from seeds found by a random walk from the
// origin) are those of buddha_generator::metropolis, the original CPU sampler.
//
// Like the naive lane, each advance() performs one orbit step, so GPU lanes never wait for each
// other's chains. A lane's chain persists across dispatches in a state buffer.

// Math from +, -, * and integer operations only: library log, sin and cos differ between the
// CPU and the GPU, and one differing acceptance would send the two chains apart. The polynomials
// are the single-precision Cephes ones.

#ifdef __METAL_VERSION__
inline uint leading_zeros(uint value) { return clz(value); }
#else
inline uint leading_zeros(uint value) { return uint(std::countl_zero(value)); }
#endif

// Natural logarithm of an integer n >= 1.
inline float log_uint(uint n) {
    BUDDHA_EXACT
    int e = 31 - int(leading_zeros(n));
    // n = 2^e * m, with m from the top 24 bits of n (exact in float), m in [1, 2).
    const uint top = e > 23 ? n >> uint(e - 23) : n << uint(23 - e);
    float m = float(top) * (1.0f / 8388608.0f);
    if (m > 1.41421356f) {
        m = m * 0.5f;
        ++e;
    }
    const float x = m - 1.0f, z = x * x;
    float y = 7.0376836292e-2f;
    y = y * x - 1.1514610310e-1f;
    y = y * x + 1.1676998740e-1f;
    y = y * x - 1.2420140846e-1f;
    y = y * x + 1.4249322787e-1f;
    y = y * x - 1.6668057665e-1f;
    y = y * x + 2.0000714765e-1f;
    y = y * x - 2.4999993993e-1f;
    y = y * x + 3.3333331174e-1f;
    y = y * x * z;
    const float fe = float(e);
    y = y - 2.12194440e-4f * fe;
    y = y - 0.5f * z;
    return x + y + 0.693359375f * fe;
}

// Direction at a uniform angle, from a 24-bit random value: the top two bits pick a quadrant and
// the rest an angle in [-pi/4, pi/4) within it.
inline void direction(uint bits, BUDDHA_THREAD float &dx, BUDDHA_THREAD float &dy) {
    BUDDHA_EXACT
    const float f = float(bits & 0x3fffffu) * (1.0f / 4194304.0f);
    const float x = (f - 0.5f) * 1.57079632679f, z = x * x;
    float s = -1.9515295891e-4f;
    s = s * z + 8.3321608736e-3f;
    s = s * z - 1.6666654611e-1f;
    s = s * z * x + x;
    float c = 2.443315711809948e-5f;
    c = c * z - 1.388731625493765e-3f;
    c = c * z + 4.166664568298827e-2f;
    c = c * z * z - 0.5f * z + 1.0f;
    switch ((bits >> 22) & 3u) {
    case 0:
        dx = c;
        dy = s;
        break;
    case 1:
        dx = -s;
        dy = c;
        break;
    case 2:
        dx = -c;
        dy = -s;
        break;
    default:
        dx = s;
        dy = -c;
        break;
    }
}

// Sampler settings beyond parameters, also a constant buffer: keep to uint and float fields.
struct metropolis_parameters {
    float radius;                 // mutation radius, in complex units
    float exponent_l, exponent_c; // target density L^a * C^b
    float chain_scale;            // proposals per chain: chain_scale * max(256 C, 2 L)
    uint seeding;                 // seeding_walk or seeding_uniform
    uint steps;                   // advance() calls per lane in a dispatch
    uint padding0, padding1;
};

enum : uint {
    seeding_walk = 0,    // random walk from the origin, restarted after walk_limit steps
    seeding_uniform = 1, // uniform over [-2, 2]^2
    walk_limit = 256,
};

template <class T> struct chain {
    enum : uint { seek, propose, evaluate_seed, evaluate_proposal, redraw };

    uint mode;
    uint random; // generator state
    uint i, last, contribute;
    T cr, ci, zr, zi; // orbit being evaluated or redrawn
    periodicity<T> period;
    T current_r, current_i; // chain state
    float current_log;      // a ln L + b ln C of the chain state
    uint left;              // proposals left in the chain
    T walk_r, walk_i;
    uint walk_steps;

    void begin(uint lane, uint key) {
        mode = seek;
        random = mix(lane * 0x9e3779b9u ^ mix(key));
        i = last = contribute = 0;
        cr = ci = zr = zi = current_r = current_i = walk_r = walk_i = T(0);
        period.reset();
        current_log = 0.0f;
        left = 0;
        walk_steps = 0;
    }

    uint next24() {
        random = random * 747796405u + 2891336453u;
        return mix(random) >> 8;
    }

    float uniform01() {
        BUDDHA_EXACT
        return float(next24()) * (1.0f / 16777216.0f);
    }

    // -ln(u) for u uniform in (0, 1].
    float exponential1() {
        BUDDHA_EXACT
        return 16.6355323f - log_uint(next24() + 1u); // 24 ln 2 - ln(k + 1)
    }

    void start(T r, T im) {
        cr = zr = r;
        ci = zi = im;
        i = contribute = 0;
        period.reset();
    }

    void step(T rr, T ii) {
        BUDDHA_EXACT
        const T next = rr - ii + cr;
        zi = T(2) * zr * zi + ci;
        zr = next;
    }

    // Picks the next seed candidate outside the exclusion map; false if none was found here.
    template <class Map>
    bool pick_seed(BUDDHA_THREAD const parameters &p, BUDDHA_THREAD const metropolis_parameters &m,
                   BUDDHA_THREAD const Map &map, BUDDHA_THREAD totals &t) {
        BUDDHA_EXACT
        T r, im;
        if (m.seeding == seeding_uniform) {
            r = T(uniform01() * 4.0f - 2.0f);
            im = T(uniform01() * 4.0f - 2.0f);
        } else {
            if (walk_steps == walk_limit) {
                walk_r = walk_i = T(0);
                walk_steps = 0;
            }
            // |N(0, 1)| approximated by four uniforms (Irwin-Hall).
            const float sum = uniform01() + uniform01() + uniform01() + uniform01();
            const float amplitude = fabs(sum - 2.0f) * 1.73205081f;
            float dx, dy;
            direction(next24(), dx, dy);
            walk_r = walk_r + T(amplitude * dx);
            walk_i = walk_i + T(amplitude * dy);
            ++walk_steps;
            r = walk_r;
            im = walk_i;
        }
        ++t.seeds;
        if (excluded(r, im, p.exclusion_size, map)) {
            ++t.excluded;
            return false;
        }
        start(r, im);
        return true;
    }

    // The ending of an evaluated orbit: its log density, or false if it cannot be drawn
    // (periodic, capped, excluded, L < 1 or no point in the window).
    bool density(bool escaped_orbit, BUDDHA_THREAD const metropolis_parameters &m,
                 BUDDHA_THREAD float &log_density) {
        BUDDHA_EXACT
        if (!escaped_orbit || i < 2 || contribute == 0)
            return false;
        log_density = m.exponent_l * log_uint(i - 1) + m.exponent_c * log_uint(contribute);
        return true;
    }

    template <class Map, class Histogram>
    void advance(BUDDHA_THREAD const parameters &p, BUDDHA_THREAD const metropolis_parameters &m,
                 BUDDHA_THREAD const geometry<T> &g, BUDDHA_THREAD const Map &map,
                 BUDDHA_THREAD Histogram &histogram, BUDDHA_THREAD totals &t) {
        BUDDHA_EXACT
        if (mode == seek) {
            if (!pick_seed(p, m, map, t))
                return;
            mode = evaluate_seed;
        } else if (mode == propose) {
            if (left == 0) {
                mode = seek;
                return;
            }
            --left;
            ++t.proposals;
            const float amplitude = exponential1() * 0.5f * uniform01() * m.radius;
            float dx, dy;
            direction(next24(), dx, dy);
            const T r = current_r + T(amplitude * dx), im = current_i + T(amplitude * dy);
            if (excluded(r, im, p.exclusion_size, map)) {
                ++t.excluded;
                return;
            }
            start(r, im);
            mode = evaluate_proposal;
        }

        const T rr = zr * zr, ii = zi * zi;
        if (mode == redraw) {
            if (i > last || escaped(rr + ii)) { // the second test only guards a diverged redraw
                t.redraw += i;
                mode = propose;
                return;
            }
            if (i >= p.low)
                t.increments +=
                    draw(g, zr, zi, in_channel(i, p.lowr, p.highr), in_channel(i, p.lowg, p.highg),
                         in_channel(i, p.lowb, p.highb), histogram);
            step(rr, ii);
            ++i;
            return;
        }

        // Evaluating a seed or a proposal: escape, periodicity and points in the window.
        uint x, y;
        if (pixel(g, zr, zi, x, y))
            ++contribute;
        bool ended = false, escaped_orbit = false;
        if (escaped(rr + ii)) {
            ended = escaped_orbit = true;
        } else if (period.cyclic(zr, zi, i)) {
            ended = true;
        } else {
            step(rr, ii);
            ended = ++i == p.high;
        }
        if (!ended)
            return;
        t.iterations += i;

        float log_density = 0.0f;
        const bool valid = density(escaped_orbit, m, log_density);
        if (!valid)
            ++t.periodic;
        if (mode == evaluate_seed) {
            if (!valid) {
                mode = seek;
                return;
            }
            current_r = cr;
            current_i = ci;
            current_log = log_density;
            const float c = float(contribute) * 256.0f, l = float(i - 1) * 2.0f;
            left = uint(m.chain_scale * (c > l ? c : l));
            walk_r = walk_i = T(0); // the next seed search starts again from the origin
            walk_steps = 0;
            ++t.chains;
            mode = propose;
            return;
        }

        if (!valid) {
            mode = propose;
            return;
        }
        // Accept with probability min(1, f' / f), in logs: ln u < ln f' - ln f.
        const float log_u = log_uint(next24() + 1u) - 16.6355323f;
        if (log_u <= log_density - current_log) {
            current_r = cr;
            current_i = ci;
            current_log = log_density;
            ++t.accepted;
        }
        ++t.escaped;
        last = i - 1 < p.high - 1 ? i - 1 : p.high - 1;
        zr = cr;
        zi = ci;
        i = 0;
        mode = redraw;
    }
};

} // namespace buddha_kernel

#endif
