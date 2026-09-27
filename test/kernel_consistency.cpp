// Checks that the CPU renderer and the shared kernel (core/buddha_kernel.h) have not drifted.
//
// The rules (escape, periodicity, exclusion cell, pixel mapping, weights, channels) are shared
// code. What is not shared is the CPU renderer's own loop: its std::complex recurrence, the stored
// orbit and the three copies of mandelbrot_base::evaluate. This test runs identical samples
// through the renderer (mandelbrot::evaluate + buddha_generator::drawPoint) and through the shared
// lane in double precision, and requires identical classifications, step counts and histograms.
// It is built with strict IEEE arithmetic, so any difference is a code difference.

#include "buddha.h"
#include "buddha_generator.h"
#include "buddha_kernel.h"

#include <boost/log/core.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using complex_type = buddha::complex_type;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

settings make_settings(uint64_t w, uint64_t h, double scale, double cre, double cim) {
    settings s{};
    s.w = w;
    s.h = h;
    s.scale = scale;
    s.cre = cre;
    s.cim = cim;
    s.lowr = 64;
    s.highr = 2048;
    s.lowg = 16;
    s.highg = 512;
    s.lowb = 4;
    s.highb = 128;
    s.contrast = s.lightness = 100;
    s.threads = 1;
    s.exclusion = BUDDHA_EXCLUSION_MAP;
    s.no_image = true;
    s.indirect_settings();
    return s;
}

struct raw_histogram {
    buddha::vector_type &raw;
    uint64_t width;
    void add(uint32_t x, uint32_t y, uint32_t channel, uint32_t weight) {
        raw[(uint64_t(y) * width + x) * 3 + channel].add(weight);
    }
};

void check(const std::string &name, const settings &s) {
    mandelbrot<complex_type> core(s);
    require(core.load(), "the committed exclusion map must load");
    const mandelbrot_base<complex_type> &base = core;

    buddha_kernel::parameters p{};
    p.low = s.low;
    p.high = s.high;
    p.lowr = s.lowr;
    p.highr = s.highr;
    p.lowg = s.lowg;
    p.highg = s.highg;
    p.lowb = s.lowb;
    p.highb = s.highb;
    p.exclusion_size = uint32_t(core.size);
    p.key0 = 0x2545f491U;
    p.key1 = 0x9e3779b9U;
    p.count = 200000;

    // CPU renderer.
    buddha::vector_type renderer(3 * s.size);
    buddha_generator generator(core, renderer, s, 1);
    buddha_kernel::totals expected{};
    vector<complex_type> &seq = generator.seq;
    vector<complex_type> copy(seq.size());
    for (uint32_t n = 0; n < p.count; ++n) {
        float cr, ci;
        buddha_kernel::sample(p, n, cr, ci);
        seq[0] = complex_type(cr, ci);
        if (core.excluded(seq[0])) {
            ++expected.excluded;
            continue;
        }
        unsigned int calculated, calculated3, contribute;
        const int orbitMax = base.evaluate(seq, calculated);
        copy[0] = seq[0];
        require(base.evaluate(copy) == orbitMax, name + ": evaluate(seq) overload drifted");
        require(base.evaluate(copy, contribute, calculated3) == orbitMax &&
                    calculated3 == calculated,
                name + ": evaluate(seq, contribute, calculated) overload drifted");
        expected.iterations += calculated;
        if (orbitMax < 0) {
            ++expected.periodic;
            continue;
        }
        ++expected.escaped;
        for (unsigned int i = s.low; int(i) <= orbitMax && i < s.high; i++)
            generator.drawPoint(seq[i], buddha_kernel::in_channel(i, s.lowr, s.highr),
                                buddha_kernel::in_channel(i, s.lowg, s.highg),
                                buddha_kernel::in_channel(i, s.lowb, s.highb));
    }

    // Shared lane, in double precision, with the renderer's geometry.
    buddha::vector_type shared(3 * s.size);
    raw_histogram histogram{shared, s.w};
    const buddha_kernel::geometry<double> g = s.histogram_geometry();
    buddha_kernel::lane<double> lane;
    lane.begin(0, 1, p.count);
    while (lane.advance(p, g, core.data, histogram)) {
    }
    const buddha_kernel::totals &actual = lane.t;

    require(actual.excluded == expected.excluded, name + ": excluded counts differ");
    require(actual.periodic == expected.periodic, name + ": periodic counts differ");
    require(actual.escaped == expected.escaped, name + ": escaped counts differ");
    require(actual.iterations == expected.iterations, name + ": step counts differ");
    uint64_t increments = 0, differing = 0;
    for (size_t i = 0; i < renderer.size(); ++i) {
        increments += renderer[i].load();
        differing += renderer[i].load() != shared[i].load();
    }
    require(actual.increments == increments, name + ": increment counts differ");
    require(differing == 0, name + ": " + std::to_string(differing) + " histogram bins differ");
    require(expected.escaped > 0 && expected.periodic > 0 && expected.excluded > 0 &&
                increments > 0,
            name + ": fixture does not exercise every path");
    std::cout << name << ": " << p.count << " samples, " << expected.escaped << " escaped, "
              << expected.periodic << " periodic, " << expected.excluded << " excluded, "
              << increments << " increments: identical\n";

    // buddha++ --sampler naive: the generator runs the float lane with the settings' kernel
    // parameters (the ones buddha-metal uses).
    buddha::vector_type naive(3 * s.size), reference(3 * s.size);
    buddha_generator sampler(core, naive, s, 2);
    sampler.naive(p.key0, p.key1, p.count);
    buddha_kernel::parameters q = s.kernel_parameters(uint32_t(core.size));
    q.key0 = p.key0;
    q.key1 = p.key1;
    q.count = p.count;
    raw_histogram reference_histogram{reference, s.w};
    const buddha_kernel::geometry<float> gf = buddha_kernel::make_geometry<float>(q);
    buddha_kernel::lane<float> float_lane;
    float_lane.begin(0, 1, q.count);
    while (float_lane.advance(q, gf, core.data, reference_histogram)) {
    }
    differing = 0;
    for (size_t i = 0; i < naive.size(); ++i)
        differing += naive[i].load() != reference[i].load();
    require(differing == 0 && sampler.computed == float_lane.t.iterations &&
                sampler.drawn_orbits == float_lane.t.escaped,
            name + ": buddha_generator::naive differs from the shared lane");
    std::cout << name << ": buddha_generator::naive matches the float lane\n";
}

} // namespace

int main() {
    boost::log::core::get()->set_logging_enabled(false);
    try {
        check("mirrored, even height", make_settings(256, 256, 64, -0.5, 0.0));
        check("mirrored, odd height", make_settings(256, 255, 64, -0.5, 0.0));
        check("off-axis", make_settings(256, 192, 256, -0.6, 0.4));
    } catch (const std::exception &error) {
        std::cerr << "kernel-consistency: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
