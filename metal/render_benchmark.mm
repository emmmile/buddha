// Naive (uniform sampling, no Metropolis) Buddhabrot benchmark: exclusion map, periodicity check
// and RGB histogram writes. It runs the same counter-hashed samples through
//   - the CPU renderer's own code (mandelbrot::evaluate and drawPoint, double precision),
//   - the shared sampling lane (core/buddha_kernel.h) on CPU threads, in float, and
//   - the Metal kernel buddha-metal renders with (the same lane on the GPU),
// and compares the resulting histograms.

#include "persistent_renderer.h"

#include "buddha.h"
#include "buddha_generator.h"
#include "mandelbrot.h"

#include <boost/log/core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace buddha_metal;
using complex_type = buddha::complex_type;
using histogram_type = buddha::vector_type;

struct config {
    uint64_t samples = 100000000;
    uint64_t width = 8192, height = 8192;
    double scale = 2048, cre = 0, cim = 0;
    uint32_t lowr = 512, highr = 8192, lowg = 128, highg = 2048, lowb = 32, highb = 512;
    uint32_t seed = 0x4d595df4U;
    uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
    std::string exclusion = "exclusion.map";
    uint64_t exclusion_size = 4096;
    bool cpu_double = true, cpu_float = true, gpu = true;
    uint64_t gpu_threads = persistent_renderer::default_threads;
    uint64_t batch = persistent_renderer::default_batch;
};

struct counters : buddha_kernel::totals {
    counters() : buddha_kernel::totals{} {}
    counters &operator+=(const buddha_kernel::totals &o) {
        iterations += o.iterations;
        redraw += o.redraw;
        escaped += o.escaped;
        excluded += o.excluded;
        periodic += o.periodic;
        increments += o.increments;
        return *this;
    }
};

double seconds_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}

template <class F> double run_threads(const config &cfg, std::vector<counters> &partial, F work) {
    const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(cfg.threads, cfg.samples));
    partial.assign(count, counters{});
    std::vector<std::thread> workers;
    const auto begin = std::chrono::steady_clock::now();
    for (uint32_t t = 0; t < count; ++t)
        workers.emplace_back([&, t, count] { work(t, count, partial[t]); });
    for (auto &worker : workers)
        worker.join();
    return seconds_since(begin);
}

// The CPU renderer's code: exclusion lookup, the double-precision loop into a stored orbit, and
// buddha_generator::drawPoint into the shared atomic histogram.
double run_cpu_double(const config &cfg, const parameters &p, mandelbrot<complex_type> &core,
                      histogram_type &raw, const settings &s, counters &result) {
    std::vector<std::unique_ptr<buddha_generator>> generators;
    for (uint32_t t = 0; t < cfg.threads; ++t)
        generators.emplace_back(std::make_unique<buddha_generator>(core, raw, s, t));
    const mandelbrot_base<complex_type> &base = core;

    std::vector<counters> partial;
    const double seconds =
        run_threads(cfg, partial, [&](uint32_t t, uint32_t lanes, counters &out) {
            buddha_generator &g = *generators[t];
            vector<complex_type> &seq = g.seq;
            for (uint64_t n = t; n < cfg.samples; n += lanes) {
                float cr, ci;
                buddha_kernel::sample(p, uint32_t(n), cr, ci);
                seq[0] = complex_type(cr, ci);
                if (core.excluded(seq[0])) {
                    ++out.excluded;
                    continue;
                }
                unsigned int calculated;
                const int orbitMax = base.evaluate(seq, calculated);
                out.iterations += calculated;
                if (orbitMax < 0) {
                    ++out.periodic;
                    continue;
                }
                ++out.escaped;
                for (unsigned int i = s.low; int(i) <= orbitMax && i < s.high; i++)
                    g.drawPoint(seq[i], buddha_kernel::in_channel(i, s.lowr, s.highr),
                                buddha_kernel::in_channel(i, s.lowg, s.highg),
                                buddha_kernel::in_channel(i, s.lowb, s.highb));
            }
        });
    for (const auto &q : partial)
        result += q;
    return seconds;
}

struct cpu_histogram {
    histogram_type &raw;
    uint64_t width;
    void add(uint32_t x, uint32_t y, uint32_t channel, uint32_t weight) {
        raw[(uint64_t(y) * width + x) * 3 + channel].add(weight);
    }
};

// The shared lane, as the GPU runs it, on CPU threads.
double run_cpu_float(const config &cfg, const parameters &p, const std::vector<uint8_t> &map,
                     histogram_type &raw, counters &result) {
    const buddha_kernel::geometry<float> g = buddha_kernel::make_geometry<float>(p);
    std::vector<counters> partial;
    const double seconds =
        run_threads(cfg, partial, [&](uint32_t t, uint32_t lanes, counters &out) {
            cpu_histogram histogram{raw, p.width};
            buddha_kernel::parameters q = p;
            q.count = uint32_t(cfg.samples);
            buddha_kernel::lane<float> l;
            l.begin(t, lanes, q.count);
            while (l.advance(q, g, map, histogram)) {
            }
            out += l.t;
        });
    for (const auto &q : partial)
        result += q;
    return seconds;
}

// Timed from the first commit to the last completion; excludes shader compilation, buffer
// creation and a warm-up dispatch.
double run_gpu(const config &cfg, const parameters &p, const std::vector<uint8_t> &map,
               histogram_type &raw, counters &result) {
    persistent_renderer gpu(default_device(), p, map.data(), map.size(), raw.data(),
                            histogram_bytes(raw), uint32_t(std::min(cfg.gpu_threads, cfg.samples)));
    persistent_renderer::wait(
        gpu.dispatch(0, uint32_t(std::min<uint64_t>(cfg.samples, 65536)), p.key0, p.key1));
    for (auto &v : raw)
        v.store(0);
    gpu.clear_totals();

    const auto begin = std::chrono::steady_clock::now();
    id<MTLCommandBuffer> last = nil;
    for (uint64_t first = 0; first < cfg.samples; first += cfg.batch)
        last = gpu.dispatch(uint32_t(first), uint32_t(std::min(cfg.batch, cfg.samples - first)),
                            p.key0, p.key1);
    persistent_renderer::wait(last);
    const double seconds = seconds_since(begin);
    result += gpu.sum();
    return seconds;
}

std::vector<uint32_t> snapshot(const histogram_type &raw) {
    std::vector<uint32_t> out(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
        out[i] = raw[i].load();
    return out;
}

// Sum bins into block x block pixel tiles per channel, to compare image shape rather than
// per-bin sampling noise.
std::vector<uint32_t> downsample(const std::vector<uint32_t> &h, uint64_t width, uint64_t block) {
    const uint64_t height = h.size() / 3 / width;
    const uint64_t bw = (width + block - 1) / block, bh = (height + block - 1) / block;
    std::vector<uint32_t> out(bw * bh * 3);
    for (uint64_t y = 0; y < height; ++y)
        for (uint64_t x = 0; x < width; ++x)
            for (uint64_t c = 0; c < 3; ++c)
                out[((y / block) * bw + x / block) * 3 + c] += h[(y * width + x) * 3 + c];
    return out;
}

// Pearson correlation and relative L1 difference over all histogram bins.
void compare_bins(const std::string &name, const std::vector<uint32_t> &a,
                  const std::vector<uint32_t> &b) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, l1 = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double x = a[i], y = b[i];
        sa += x;
        sb += y;
        saa += x * x;
        sbb += y * y;
        sab += x * y;
        l1 += std::fabs(x - y);
    }
    const double n = double(a.size());
    const double r = (sab - sa * sb / n) / std::sqrt((saa - sa * sa / n) * (sbb - sb * sb / n));
    std::cout << std::fixed << "  " << name << ": correlation " << std::setprecision(6) << r
              << ", relative L1 difference " << std::setprecision(2) << 100.0 * l1 / sa << "%\n"
              << std::defaultfloat;
}

void compare(const std::string &name, const std::vector<uint32_t> &a,
             const std::vector<uint32_t> &b, uint64_t width) {
    if (a.empty() || b.empty())
        return;
    compare_bins(name + ", per bin", a, b);
    compare_bins(name + ", 16x16 blocks", downsample(a, width, 16), downsample(b, width, 16));
}

void report(const std::string &name, const counters &t, double seconds, const config &cfg) {
    std::cout << std::fixed << std::setprecision(3) << name << ": " << seconds << " s, "
              << double(cfg.samples) / seconds / 1e6 << " M samples/s, "
              << double(t.iterations) / seconds / 1e9 << " G steps/s, "
              << double(t.increments) / seconds / 1e6 << " M histogram increments/s\n"
              << std::defaultfloat << "  excluded " << t.excluded << ", periodic/capped "
              << t.periodic << ", escaped " << t.escaped << ", steps " << t.iterations
              << ", redraw steps " << t.redraw << ", increments " << t.increments << "\n";
}

uint64_t parse_uint(const std::string &value, const std::string &option) {
    const unsigned long long parsed = std::stoull(value);
    if (parsed == 0)
        throw std::runtime_error(option + " must be positive");
    return parsed;
}

config parse_arguments(int argc, char **argv) {
    config cfg;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout << "usage: metal-render-benchmark [--samples N] [--width N] [--height N]\n"
                         "  [--scale X] [--cre X] [--cim X] [--seed N] [--threads N]\n"
                         "  [--red-min N] [--red-max N] [--green-min N] [--green-max N]\n"
                         "  [--blue-min N] [--blue-max N]\n"
                         "  [--exclusion-map PATH|none] [--exclusion-size N]\n"
                         "  [--no-cpu-double] [--no-cpu-float] [--no-gpu]\n"
                         "  [--gpu-threads N] [--batch N]\n";
            std::exit(0);
        }
        if (option == "--no-cpu-double") {
            cfg.cpu_double = false;
            continue;
        }
        if (option == "--no-cpu-float") {
            cfg.cpu_float = false;
            continue;
        }
        if (option == "--no-gpu") {
            cfg.gpu = false;
            continue;
        }
        if (index + 1 == argc)
            throw std::runtime_error("missing value for " + option);
        const std::string value = argv[++index];
        if (option == "--samples")
            cfg.samples = parse_uint(value, option);
        else if (option == "--width")
            cfg.width = parse_uint(value, option);
        else if (option == "--height")
            cfg.height = parse_uint(value, option);
        else if (option == "--scale")
            cfg.scale = std::stod(value);
        else if (option == "--cre")
            cfg.cre = std::stod(value);
        else if (option == "--cim")
            cfg.cim = std::stod(value);
        else if (option == "--seed")
            cfg.seed = uint32_t(std::stoul(value));
        else if (option == "--threads")
            cfg.threads = uint32_t(parse_uint(value, option));
        else if (option == "--red-min")
            cfg.lowr = uint32_t(std::stoul(value));
        else if (option == "--red-max")
            cfg.highr = uint32_t(parse_uint(value, option));
        else if (option == "--green-min")
            cfg.lowg = uint32_t(std::stoul(value));
        else if (option == "--green-max")
            cfg.highg = uint32_t(parse_uint(value, option));
        else if (option == "--blue-min")
            cfg.lowb = uint32_t(std::stoul(value));
        else if (option == "--blue-max")
            cfg.highb = uint32_t(parse_uint(value, option));
        else if (option == "--exclusion-map")
            cfg.exclusion = value;
        else if (option == "--exclusion-size")
            cfg.exclusion_size = parse_uint(value, option);
        else if (option == "--gpu-threads")
            cfg.gpu_threads = parse_uint(value, option);
        else if (option == "--batch")
            cfg.batch = parse_uint(value, option);
        else
            throw std::runtime_error("unknown option: " + option);
    }
    if (!(cfg.scale > 0) || !std::isfinite(cfg.scale))
        throw std::runtime_error("--scale must be positive and finite");
    if (cfg.samples > (uint64_t(1) << 31) || cfg.batch > (uint64_t(1) << 31))
        throw std::runtime_error("--samples and --batch must be at most 2^31");
    return cfg;
}

settings make_settings(const config &cfg) {
    settings s{};
    s.w = cfg.width;
    s.h = cfg.height;
    s.scale = cfg.scale;
    s.cre = cfg.cre;
    s.cim = cfg.cim;
    s.lowr = cfg.lowr;
    s.highr = cfg.highr;
    s.lowg = cfg.lowg;
    s.highg = cfg.highg;
    s.lowb = cfg.lowb;
    s.highb = cfg.highb;
    s.contrast = s.lightness = 100;
    s.threads = cfg.threads;
    s.exclusion = cfg.exclusion == "none" ? "" : cfg.exclusion;
    s.exclusion_size = cfg.exclusion_size;
    s.formula = "z = z * z + c";
    s.no_image = true;
    s.indirect_settings();
    return s;
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            boost::log::core::get()->set_logging_enabled(false);
            const config cfg = parse_arguments(argc, argv);
            const settings s = make_settings(cfg);
            mandelbrot<complex_type> core(s);

            // The benchmark may create a map; the renderers only load one.
            if (s.exclusion.empty()) {
                std::cout << "exclusion map: disabled\n";
            } else if (core.load()) {
                std::cout << "exclusion map: loaded " << s.exclusion << "\n";
            } else {
                std::cout << "exclusion map: generating " << cfg.exclusion_size << " map into "
                          << s.exclusion << " ..." << std::flush;
                const auto begin = std::chrono::steady_clock::now();
                core.exclusion();
                core.save();
                std::cout << " " << std::fixed << std::setprecision(1) << seconds_since(begin)
                          << " s\n"
                          << std::defaultfloat;
            }

            parameters p = make_parameters(s);
            p.key0 = buddha_kernel::mix(cfg.seed);
            p.key1 = buddha_kernel::mix(cfg.seed ^ 0x9e3779b9U);

            const uint64_t bins = s.w * s.histogram_height * 3;
            std::cout << "naive Buddhabrot benchmark: " << cfg.samples << " samples, " << s.w << "x"
                      << s.h << " (histogram " << s.w << "x" << s.histogram_height << ", "
                      << bins * 4 / (1024 * 1024) << " MiB), scale " << s.scale << ", iterations "
                      << s.low << "-" << s.high << ", " << cfg.threads << " CPU threads\n";

            std::vector<uint32_t> h_double, h_float, h_gpu;
            if (cfg.cpu_double) {
                histogram_type raw(bins);
                counters t;
                const double seconds = run_cpu_double(cfg, p, core, raw, s, t);
                h_double = snapshot(raw);
                for (uint32_t v : h_double)
                    t.increments += v;
                report("CPU double (renderer code)", t, seconds, cfg);
            }
            if (cfg.cpu_float) {
                histogram_type raw(bins);
                counters t;
                const double seconds = run_cpu_float(cfg, p, core.data, raw, t);
                h_float = snapshot(raw);
                report("CPU float (shared lane)", t, seconds, cfg);
            }
            if (cfg.gpu) {
                histogram_type raw(bins);
                counters t;
                const double seconds = run_gpu(cfg, p, core.data, raw, t);
                h_gpu = snapshot(raw);
                report(std::string("Metal float (") + default_device().name.UTF8String + ")", t,
                       seconds, cfg);
            }

            std::cout << "histogram agreement:\n";
            compare("CPU double vs Metal", h_double, h_gpu, s.w);
            compare("CPU float vs Metal", h_float, h_gpu, s.w);
            compare("CPU double vs CPU float", h_double, h_float, s.w);
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "metal-render-benchmark: " << error.what() << "\n";
            return 1;
        }
    }
}
