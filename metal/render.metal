// buddha-metal's GPU kernel. The rendering rules and the sampling lane come from
// core/buddha_kernel.h, shared with the CPU renderer; the build inlines that header here and
// embeds the result, which the Metal framework compiles at run time.

#include <metal_stdlib>
using namespace metal;

// Round every operation separately, as the CPU does: fusing a*b + c differently in the two orbit
// passes would let the redraw drift from the orbit that was tested.
#pragma clang fp contract(off)

#include "buddha_kernel.h"

using namespace buddha_kernel;

// Exclusion map and histogram access for the shared lane.
struct device_map {
    device const uchar *data;
    uchar operator[](uint i) const { return data[i]; }
};

struct device_histogram {
    device atomic_uint *data;
    uint width;
    void add(uint x, uint y, uint channel, uint weight) {
        atomic_fetch_add_explicit(&data[(y * width + x) * 3u + channel], weight,
                                  memory_order_relaxed);
    }
};

// Persistent threads: each thread runs one lane over samples tid, tid + threads, ... of the
// dispatch (see lane in buddha_kernel.h for why).
kernel void render(constant parameters &params [[buffer(0)]],
                   device const uchar *map [[buffer(1)]],
                   device atomic_uint *raw [[buffer(2)]],
                   device totals *out [[buffer(3)]],
                   uint tid [[thread_position_in_grid]]) {
    const parameters p = params;
    if (tid >= p.threads)
        return;
    const geometry<float> g = make_geometry<float>(p);
    const device_map exclusion = {map};
    device_histogram histogram = {raw, p.width};

    lane<float> l;
    l.begin(tid, p.threads, p.count);
    while (l.advance(p, g, exclusion, histogram)) {
    }

    // Each thread owns its slot across dispatches, which run in order on one queue.
    out[tid].iterations += l.t.iterations;
    out[tid].redraw += l.t.redraw;
    out[tid].escaped += l.t.escaped;
    out[tid].excluded += l.t.excluded;
    out[tid].periodic += l.t.periodic;
    out[tid].increments += l.t.increments;
}

// Metropolis chains: each thread advances its own chain (chain in buddha_kernel.h) a fixed number
// of steps, so every thread does the same work, and saves it for the next dispatch.
kernel void metropolis(constant parameters &params [[buffer(0)]],
                       device const uchar *map [[buffer(1)]],
                       device atomic_uint *raw [[buffer(2)]],
                       device totals *out [[buffer(3)]],
                       constant metropolis_parameters &settings [[buffer(4)]],
                       device chain<float> *chains [[buffer(5)]],
                       uint tid [[thread_position_in_grid]]) {
    const parameters p = params;
    const metropolis_parameters m = settings;
    if (tid >= p.threads)
        return;
    const geometry<float> g = make_geometry<float>(p);
    const device_map exclusion = {map};
    device_histogram histogram = {raw, p.width};

    chain<float> c = chains[tid];
    totals t;
    t.iterations = t.redraw = t.escaped = t.excluded = t.periodic = t.increments = 0;
    t.proposals = t.accepted = t.seeds = t.chains = 0;
    for (uint s = 0; s < m.steps; ++s)
        c.advance(p, m, g, exclusion, histogram, t);
    chains[tid] = c;

    out[tid].iterations += t.iterations;
    out[tid].redraw += t.redraw;
    out[tid].escaped += t.escaped;
    out[tid].excluded += t.excluded;
    out[tid].periodic += t.periodic;
    out[tid].increments += t.increments;
    out[tid].proposals += t.proposals;
    out[tid].accepted += t.accepted;
    out[tid].seeds += t.seeds;
    out[tid].chains += t.chains;
}
