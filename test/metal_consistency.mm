// Checks that buddha-metal's GPU kernel computes what the shared lane (core/buddha_kernel.h)
// computes on the CPU: identical samples, float precision, strict IEEE arithmetic on both sides.
// Exits with 77 (skipped) when the system has no Metal device.

#include "persistent_renderer.h"

#include "buddha.h"
#include "buddha_generator.h"

#include <boost/log/core.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace buddha_metal;

struct raw_histogram {
    buddha::vector_type &raw;
    uint64_t width;
    void add(uint32_t x, uint32_t y, uint32_t channel, uint32_t weight) {
        raw[(uint64_t(y) * width + x) * 3 + channel].add(weight);
    }
};

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
    s.threads = 1;
    s.exclusion = BUDDHA_EXCLUSION_MAP;
    s.indirect_settings();
    return s;
}

// Returns the number of histogram bins that differ.
uint64_t check(id<MTLDevice> device, const std::string &name, const settings &s) {
    mandelbrot<buddha::complex_type> core(s);
    if (!core.load())
        throw std::runtime_error("the committed exclusion map must load");

    parameters p = make_parameters(s, core.size);
    p.key0 = 0x2545f491U;
    p.key1 = 0x9e3779b9U;
    const uint32_t samples = 1 << 20;

    buddha::vector_type cpu(3 * s.size), gpu(3 * s.size);
    raw_histogram histogram{cpu, s.w};
    const buddha_kernel::geometry<float> g = buddha_kernel::make_geometry<float>(p);
    buddha_kernel::parameters q = p;
    q.count = samples;
    buddha_kernel::lane<float> lane;
    lane.begin(0, 1, samples);
    while (lane.advance(q, g, core.data, histogram)) {
    }
    const totals expected = lane.t;

    persistent_renderer renderer(device, p, core.data.data(), core.data.size(), gpu.data(),
                                 histogram_bytes(gpu), 4096);
    persistent_renderer::wait(renderer.dispatch(0, samples, p.key0, p.key1));
    const totals actual = renderer.sum();

    // buddha++ --sampler naive, given the same key.
    buddha::vector_type naive(3 * s.size);
    buddha_generator generator(core, naive, s, 1);
    generator.naive(p.key0, p.key1, samples);

    uint64_t differing = 0;
    for (size_t i = 0; i < cpu.size(); ++i)
        differing += (cpu[i].load() != gpu[i].load()) + (naive[i].load() != gpu[i].load());
    if (generator.computed != actual.iterations || generator.drawn_orbits != actual.escaped)
        throw std::runtime_error(name + ": buddha_generator::naive differs from the GPU");
    std::cout << name << ": CPU " << expected.escaped << " escaped, " << expected.periodic
              << " periodic, " << expected.excluded << " excluded, " << expected.iterations
              << " steps, " << expected.increments << " increments; GPU " << actual.escaped << ", "
              << actual.periodic << ", " << actual.excluded << ", " << actual.iterations << ", "
              << actual.increments << "; " << differing << " bins differ\n";
    if (actual.excluded != expected.excluded || actual.escaped != expected.escaped ||
        actual.periodic != expected.periodic || actual.iterations != expected.iterations ||
        actual.redraw != expected.redraw || actual.increments != expected.increments)
        throw std::runtime_error(name + ": GPU counters differ from the shared lane on the CPU");
    return differing;
}

// Metropolis chains: the GPU runs threads chains over two dispatches, the CPU the same chains one
// after another. Returns the number of histogram bins that differ.
uint64_t check_metropolis(id<MTLDevice> device, const std::string &name, const settings &s,
                          uint32_t seeding) {
    mandelbrot<buddha::complex_type> core(s);
    if (!core.load())
        throw std::runtime_error("the committed exclusion map must load");

    const uint32_t threads = 256, dispatches = 2, key = 0x2545f491U;
    parameters p = make_parameters(s, core.size);
    p.threads = threads;
    buddha_kernel::metropolis_parameters m{};
    m.radius = float(20.0 / s.scale);
    m.exponent_l = 2;
    m.exponent_c = 1;
    m.chain_scale = 0.05f; // short chains, so the test also covers many seed searches
    m.seeding = seeding;
    m.steps = 20000;

    buddha::vector_type cpu(3 * s.size), gpu(3 * s.size);
    raw_histogram histogram{cpu, s.w};
    const buddha_kernel::geometry<float> g = buddha_kernel::make_geometry<float>(p);
    totals expected{};
    for (uint32_t lane = 0; lane < threads; ++lane) {
        metropolis_chain c;
        c.begin(lane, key);
        for (uint32_t step = 0; step < dispatches * m.steps; ++step)
            c.advance(p, m, g, core.data, histogram, expected);
    }

    persistent_renderer renderer(device, p, core.data.data(), core.data.size(), gpu.data(),
                                 histogram_bytes(gpu), threads);
    renderer.begin_chains(key);
    for (uint32_t d = 0; d < dispatches; ++d)
        persistent_renderer::wait(renderer.dispatch_metropolis(m));
    const totals actual = renderer.sum();

    uint64_t differing = 0;
    for (size_t i = 0; i < cpu.size(); ++i)
        differing += cpu[i].load() != gpu[i].load();
    std::cout << name << ": CPU " << expected.chains << " chains, " << expected.proposals
              << " proposals, " << expected.accepted << " accepted, " << expected.increments
              << " increments; GPU " << actual.chains << ", " << actual.proposals << ", "
              << actual.accepted << ", " << actual.increments << "; " << differing
              << " bins differ\n";
    if (expected.chains == 0 || expected.accepted == 0 || expected.increments == 0)
        throw std::runtime_error(name + ": the chains drew nothing");
    if (actual.chains != expected.chains || actual.seeds != expected.seeds ||
        actual.proposals != expected.proposals || actual.accepted != expected.accepted ||
        actual.excluded != expected.excluded || actual.escaped != expected.escaped ||
        actual.periodic != expected.periodic || actual.iterations != expected.iterations ||
        actual.redraw != expected.redraw || actual.increments != expected.increments)
        throw std::runtime_error(name + ": GPU counters differ from the chains on the CPU");
    return differing;
}

} // namespace

int main() {
    @autoreleasepool {
        boost::log::core::get()->set_logging_enabled(false);
        id<MTLDevice> device = find_device();
        if (device == nil) {
            std::cout << "no Metal device: skipped\n";
            return 77;
        }
        try {
            uint64_t differing =
                check(device, "mirrored, even height", make_settings(256, 256, 64, -0.5, 0.0));
            differing +=
                check(device, "mirrored, odd height", make_settings(256, 255, 64, -0.5, 0.0));
            differing += check(device, "off-axis", make_settings(256, 192, 256, -0.6, 0.4));
            differing += check_metropolis(device, "metropolis, walk seeds",
                                          make_settings(256, 255, 64, -0.5, 0.0),
                                          buddha_kernel::seeding_walk);
            differing += check_metropolis(device, "metropolis, uniform seeds, off-axis",
                                          make_settings(256, 192, 256, -0.6, 0.4),
                                          buddha_kernel::seeding_uniform);
            if (differing != 0)
                throw std::runtime_error(std::to_string(differing) + " histogram bins differ");
        } catch (const std::exception &error) {
            std::cerr << "metal-consistency: " << error.what() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
}
