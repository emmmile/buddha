// Naive (uniform sampling, no Metropolis) Buddhabrot benchmark: exclusion map, periodicity check
// and RGB histogram writes, on the CPU and on Metal. Every implementation evaluates the same
// counter-hashed sample points, so their histograms can be compared directly. The persistent
// Metal path is the kernel buddha-metal renders with.

#include "persistent_renderer.h"

#include "buddha.h"
#include "buddha_generator.h"
#include "mandelbrot.h"

#include <boost/log/core.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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
    uint64_t batch = 4194304; // simple kernel: samples (threads) per dispatch
    uint64_t width = 8192, height = 8192;
    double scale = 2048, cre = 0, cim = 0;
    uint32_t lowr = 512, highr = 8192, lowg = 128, highg = 2048, lowb = 32, highb = 512;
    uint32_t seed = 0x4d595df4U;
    uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
    std::string exclusion = "exclusion.map";
    uint64_t exclusion_size = 4096;
    bool cpu_double = true, cpu_float = true;
    bool gpu_simple = true, gpu_persistent = true;
    uint64_t gpu_threads = persistent_renderer::default_threads;
    uint64_t persistent_batch = persistent_renderer::default_batch;
};

struct totals {
    uint64_t iterations = 0, redraw = 0, escaped = 0, excluded = 0, periodic = 0, increments = 0;

    template <class T> totals &operator+=(const T &o) {
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

template <class F> double run_threads(const config &cfg, std::vector<totals> &partial, F work) {
    const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(cfg.threads, cfg.samples));
    partial.assign(count, totals{});
    std::vector<std::thread> workers;
    const auto begin = std::chrono::steady_clock::now();
    for (uint32_t t = 0; t < count; ++t) {
        const uint64_t first = cfg.samples * t / count, last = cfg.samples * (t + 1) / count;
        workers.emplace_back([&, t, first, last] { work(t, first, last, partial[t]); });
    }
    for (auto &worker : workers)
        worker.join();
    return seconds_since(begin);
}

// The production renderer path: exclusion check, the double-precision periodicity loop into a
// stored orbit, and buddha_generator::drawPoint into the shared atomic histogram.
double run_cpu_double(const config &cfg, const parameters &p, mandelbrot<complex_type> &core,
                      histogram_type &raw, const settings &s, totals &result) {
    std::vector<std::unique_ptr<buddha_generator>> generators;
    for (uint32_t t = 0; t < cfg.threads; ++t)
        generators.emplace_back(std::make_unique<buddha_generator>(core, raw, s, t));
    const mandelbrot_base<complex_type> &base = core;

    std::vector<totals> partial;
    const double seconds =
        run_threads(cfg, partial, [&](uint32_t t, uint64_t first, uint64_t last, totals &out) {
            buddha_generator &g = *generators[t];
            vector<complex_type> &seq = g.seq;
            for (uint64_t n = first; n < last; ++n) {
                float cr, ci;
                sample_point(p, static_cast<uint32_t>(n), cr, ci);
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
                    g.drawPoint(seq[i], i < s.highr && i > s.lowr, i < s.highg && i > s.lowg,
                                i < s.highb && i > s.lowb);
            }
        });
    for (const auto &q : partial)
        result += q;
    return seconds;
}

// ---- the Metal kernel's algorithm, on the CPU, for a like-for-like hardware comparison ----

bool excluded_float(const parameters &p, const uint8_t *map, float cr, float ci) {
    const float size = float(p.exclusion_size);
    const int x = int(cr * size / 4.0f + size / 2.0f);
    const int y = int(-std::fabs(ci) * size / 4.0f + size / 2.0f);
    if (x < 0 || x >= int(p.exclusion_size) || y < 0 || y >= int(p.exclusion_size / 2))
        return false;
    return map[uint32_t(y) * p.exclusion_size + uint32_t(x)] != 0;
}

void draw_float(const parameters &p, histogram_type &raw, float zr, float zi, uint32_t i,
                uint64_t &increments) {
    const float image_x = (zr - p.minre) * p.scale;
    if (!(image_x >= 0.0f && image_x < float(p.width)))
        return;
    const float imag = p.symmetric ? std::fabs(zi) : zi;
    const float image_y = (p.maxim - imag) * p.scale;
    if (!(image_y >= 0.0f && image_y < float(p.histogram_height)))
        return;
    const uint32_t x = uint32_t(image_x), y = uint32_t(image_y);
    const uint64_t index = (uint64_t(y) * p.width + x) * 3;
    const uint32_t weight = p.odd_center && y + 1 == p.histogram_height ? 2 : 1;
    if (i < p.highr && i > p.lowr) {
        raw[index].add(weight);
        increments += weight;
    }
    if (i < p.highg && i > p.lowg) {
        raw[index + 1].add(weight);
        increments += weight;
    }
    if (i < p.highb && i > p.lowb) {
        raw[index + 2].add(weight);
        increments += weight;
    }
}

void evaluate_float(const parameters &p, const uint8_t *map, histogram_type &raw, uint32_t n,
                    totals &out) {
    // The project builds with -ffast-math. Keep this function strict so the redraw pass computes
    // exactly the orbit the first pass tested, as the Metal kernel (safe math mode) does.
#pragma clang fp contract(off) reassociate(off)
    float cr, ci;
    sample_point(p, n, cr, ci);
    if (excluded_float(p, map, cr, ci)) {
        ++out.excluded;
        return;
    }

    // Pass 1: escape and periodicity, mirroring mandelbrot_base::evaluate and cyclic.
    const float epsilon2 = FLT_EPSILON * FLT_EPSILON;
    float zr = cr, zi = ci, pr = 0, pi = 0;
    uint32_t criticalStep = 8, i = 0;
    int orbitMax = -1;
    for (; i < p.high; ++i) {
        if (zr * zr + zi * zi > 8.0f) {
            orbitMax = int(i) - 1;
            break;
        }
        if (i == 8) {
            pr = zr;
            pi = zi;
        } else if (i > criticalStep) {
            const float dr = zr - pr, di = zi - pi;
            if (dr * dr + di * di < epsilon2)
                break;
            if (i == criticalStep * 2) {
                criticalStep *= 2;
                pr = zr;
                pi = zi;
            }
        }
        const float next = zr * zr - zi * zi + cr;
        zi = 2.0f * zr * zi + ci;
        zr = next;
    }
    out.iterations += i;
    if (orbitMax < 0) {
        ++out.periodic;
        return;
    }
    ++out.escaped;

    // Pass 2: re-iterate the escaping orbit and draw it (nothing is stored per orbit).
    zr = cr;
    zi = ci;
    const uint32_t last = std::min<uint32_t>(uint32_t(orbitMax), p.high - 1);
    uint32_t j = 0;
    for (; j <= last; ++j) {
        if (zr * zr + zi * zi > 8.0f)
            break; // never iterate past escape, even if the redraw diverged
        if (j >= p.low)
            draw_float(p, raw, zr, zi, j, out.increments);
        const float next = zr * zr - zi * zi + cr;
        zi = 2.0f * zr * zi + ci;
        zr = next;
    }
    out.redraw += j;
}

double run_cpu_float(const config &cfg, const parameters &p, const uint8_t *map,
                     histogram_type &raw, totals &result) {
    std::vector<totals> partial;
    const double seconds =
        run_threads(cfg, partial, [&](uint32_t, uint64_t first, uint64_t last, totals &out) {
            for (uint64_t n = first; n < last; ++n)
                evaluate_float(p, map, raw, uint32_t(n), out);
        });
    for (const auto &q : partial)
        result += q;
    return seconds;
}

// One sample per thread, for comparison with the persistent kernel. It reuses the shared
// sampling, exclusion and draw functions from render_kernel.h.
const char *simple_kernel_source = R"metal(
struct GroupTotals {
    uint iterations, redraw, escaped, excluded, periodic, increments, pad0, pad1;
};

kernel void render_simple(
    constant Parameters &p [[buffer(0)]],
    device const uchar *map [[buffer(1)]],
    device atomic_uint *raw [[buffer(2)]],
    device GroupTotals *groups [[buffer(3)]],
    uint tid [[thread_position_in_grid]],
    uint local_id [[thread_index_in_threadgroup]],
    uint group_id [[threadgroup_position_in_grid]]) {
    threadgroup atomic_uint sums[6];
    if (local_id < 6u)
        atomic_store_explicit(&sums[local_id], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint iterations = 0u, redraw = 0u, escaped = 0u, excluded_count = 0u, periodic = 0u,
         increments = 0u;
    if (tid < p.count) {
        const float2 c = sample_point(p, p.offset + tid);
        if (excluded(p, map, c)) {
            excluded_count = 1u;
        } else {
            const float epsilon2 = FLT_EPSILON * FLT_EPSILON;
            float zr = c.x, zi = c.y, pr = 0.0f, pi = 0.0f;
            uint criticalStep = 8u, i = 0u;
            int orbitMax = -1;
            for (; i < p.high; ++i) {
                if (zr * zr + zi * zi > 8.0f) {
                    orbitMax = int(i) - 1;
                    break;
                }
                if (i == 8u) {
                    pr = zr;
                    pi = zi;
                } else if (i > criticalStep) {
                    const float dr = zr - pr, di = zi - pi;
                    if (dr * dr + di * di < epsilon2)
                        break;
                    if (i == criticalStep * 2u) {
                        criticalStep *= 2u;
                        pr = zr;
                        pi = zi;
                    }
                }
                const float next = zr * zr - zi * zi + c.x;
                zi = 2.0f * zr * zi + c.y;
                zr = next;
            }
            iterations = i;
            if (orbitMax < 0) {
                periodic = 1u;
            } else {
                escaped = 1u;
                zr = c.x;
                zi = c.y;
                const uint last = min(uint(orbitMax), p.high - 1u);
                uint j = 0u;
                for (; j <= last; ++j) {
                    if (zr * zr + zi * zi > 8.0f)
                        break;
                    if (j >= p.low)
                        draw(p, raw, zr, zi, j, increments);
                    const float next = zr * zr - zi * zi + c.x;
                    zi = 2.0f * zr * zi + c.y;
                    zr = next;
                }
                redraw = j;
            }
        }
    }

    atomic_fetch_add_explicit(&sums[0], iterations, memory_order_relaxed);
    atomic_fetch_add_explicit(&sums[1], redraw, memory_order_relaxed);
    atomic_fetch_add_explicit(&sums[2], escaped, memory_order_relaxed);
    atomic_fetch_add_explicit(&sums[3], excluded_count, memory_order_relaxed);
    atomic_fetch_add_explicit(&sums[4], periodic, memory_order_relaxed);
    atomic_fetch_add_explicit(&sums[5], increments, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (local_id == 0u) {
        const uint g = p.offset / 256u + group_id;
        groups[g].iterations = atomic_load_explicit(&sums[0], memory_order_relaxed);
        groups[g].redraw = atomic_load_explicit(&sums[1], memory_order_relaxed);
        groups[g].escaped = atomic_load_explicit(&sums[2], memory_order_relaxed);
        groups[g].excluded = atomic_load_explicit(&sums[3], memory_order_relaxed);
        groups[g].periodic = atomic_load_explicit(&sums[4], memory_order_relaxed);
        groups[g].increments = atomic_load_explicit(&sums[5], memory_order_relaxed);
    }
}
)metal";

struct group_totals {
    uint32_t iterations, redraw, escaped, excluded, periodic, increments, pad0, pad1;
};

// GPU timings run from the first commit to the last completion. They exclude shader
// compilation, buffer allocation and clearing, and a warm-up dispatch.
double run_gpu_simple(const config &cfg, const parameters &p, const std::vector<uint8_t> &map,
                      std::vector<uint32_t> &histogram, totals &result) {
    id<MTLDevice> device = default_device();
    id<MTLLibrary> library = compile(device, std::string(kernel_source) + simple_kernel_source);
    id<MTLComputePipelineState> state = pipeline(device, library, @"render_simple");
    id<MTLCommandQueue> queue = [device newCommandQueue];

    const uint64_t histogram_bytes = uint64_t(p.width) * p.histogram_height * 3 * sizeof(uint32_t);
    const uint64_t group_count = (cfg.samples + group_size - 1) / group_size;
    id<MTLBuffer> map_buffer = [device newBufferWithBytes:map.data()
                                                   length:map.size()
                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> raw = [device newBufferWithLength:histogram_bytes
                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> groups = [device newBufferWithLength:group_count * sizeof(group_totals)
                                               options:MTLResourceStorageModeShared];
    if (map_buffer == nil || raw == nil || groups == nil)
        throw std::runtime_error("unable to allocate Metal buffers");

    auto dispatch = [&](uint64_t first, uint64_t count) {
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        parameters q = p;
        q.offset = uint32_t(first);
        q.count = uint32_t(count);
        [encoder setComputePipelineState:state];
        [encoder setBytes:&q length:sizeof(q) atIndex:0];
        [encoder setBuffer:map_buffer offset:0 atIndex:1];
        [encoder setBuffer:raw offset:0 atIndex:2];
        [encoder setBuffer:groups offset:0 atIndex:3];
        [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
        [encoder endEncoding];
        [command commit];
        return command;
    };

    persistent_renderer::wait(dispatch(0, std::min<uint64_t>(cfg.samples, 65536)));
    std::memset(raw.contents, 0, histogram_bytes);
    std::memset(groups.contents, 0, group_count * sizeof(group_totals));

    const auto begin = std::chrono::steady_clock::now();
    id<MTLCommandBuffer> last = nil;
    for (uint64_t first = 0; first < cfg.samples; first += cfg.batch)
        last = dispatch(first, std::min(cfg.batch, cfg.samples - first));
    persistent_renderer::wait(last);
    const double seconds = seconds_since(begin);

    const auto *g = static_cast<const group_totals *>(groups.contents);
    for (uint64_t i = 0; i < group_count; ++i)
        result += g[i];
    const auto *h = static_cast<const uint32_t *>(raw.contents);
    histogram.assign(h, h + histogram_bytes / sizeof(uint32_t));
    return seconds;
}

double run_gpu_persistent(const config &cfg, const parameters &p, const std::vector<uint8_t> &map,
                          std::vector<uint32_t> &histogram, totals &result) {
    persistent_renderer gpu(default_device(), p, map.data(), map.size(),
                            uint64_t(p.width) * p.histogram_height * 3,
                            uint32_t(std::min(cfg.gpu_threads, cfg.samples)));
    persistent_renderer::wait(
        gpu.dispatch(0, uint32_t(std::min<uint64_t>(cfg.samples, 65536)), p.key0, p.key1));
    gpu.clear();

    const auto begin = std::chrono::steady_clock::now();
    id<MTLCommandBuffer> last = nil;
    for (uint64_t first = 0; first < cfg.samples; first += cfg.persistent_batch)
        last = gpu.dispatch(uint32_t(first),
                            uint32_t(std::min(cfg.persistent_batch, cfg.samples - first)), p.key0,
                            p.key1);
    persistent_renderer::wait(last);
    const double seconds = seconds_since(begin);

    result += gpu.totals();
    histogram.assign(gpu.histogram(), gpu.histogram() + gpu.bins());
    return seconds;
}

std::vector<uint32_t> snapshot(const histogram_type &raw) {
    std::vector<uint32_t> out(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
        out[i] = raw[i].load();
    return out;
}

uint64_t sum(const std::vector<uint32_t> &h) {
    uint64_t total = 0;
    for (uint32_t v : h)
        total += v;
    return total;
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
    constexpr uint64_t block = 16;
    compare_bins(name + ", per bin", a, b);
    compare_bins(name + ", 16x16 blocks", downsample(a, width, block), downsample(b, width, block));
}

void report(const std::string &name, const totals &t, double seconds, const config &cfg) {
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
                         "  [--no-cpu-double] [--no-cpu-float] [--gpu-kernel "
                         "simple|persistent|both|none]\n"
                         "  [--batch N] [--gpu-threads N] [--persistent-batch N]\n";
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
        if (index + 1 == argc)
            throw std::runtime_error("missing value for " + option);
        const std::string value = argv[++index];
        if (option == "--samples")
            cfg.samples = parse_uint(value, option);
        else if (option == "--batch")
            cfg.batch = parse_uint(value, option);
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
        else if (option == "--persistent-batch")
            cfg.persistent_batch = parse_uint(value, option);
        else if (option == "--gpu-kernel") {
            if (value != "simple" && value != "persistent" && value != "both" && value != "none")
                throw std::runtime_error("--gpu-kernel must be simple, persistent, both or none");
            cfg.gpu_simple = value == "simple" || value == "both";
            cfg.gpu_persistent = value == "persistent" || value == "both";
        } else
            throw std::runtime_error("unknown option: " + option);
    }
    if (!(cfg.scale > 0) || !std::isfinite(cfg.scale))
        throw std::runtime_error("--scale must be positive and finite");
    if (cfg.samples > (uint64_t(1) << 31) || cfg.persistent_batch > (uint64_t(1) << 31))
        throw std::runtime_error("--samples and --persistent-batch must be at most 2^31");
    cfg.batch = (cfg.batch + group_size - 1) / group_size * group_size;
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

            if (s.exclusion.empty()) {
                std::cout << "exclusion map: disabled\n";
            } else if (core.load()) {
                std::cout << "exclusion map: loaded " << s.exclusion << " (" << cfg.exclusion_size
                          << ")\n";
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
            p.key0 = mix(cfg.seed);
            p.key1 = mix(cfg.seed ^ 0x9e3779b9U);

            const uint64_t bins = s.w * s.histogram_height * 3;
            std::cout << "naive Buddhabrot benchmark: " << cfg.samples << " samples, " << s.w << "x"
                      << s.h << " (histogram " << s.w << "x" << s.histogram_height << ", "
                      << bins * 4 / (1024 * 1024) << " MiB), scale " << s.scale << ", iterations "
                      << s.low << "-" << s.high << ", " << cfg.threads << " CPU threads\n";

            std::vector<uint32_t> h_double, h_float, h_simple, h_persistent;
            if (cfg.cpu_double) {
                histogram_type raw(bins);
                totals t;
                const double seconds = run_cpu_double(cfg, p, core, raw, s, t);
                h_double = snapshot(raw);
                t.increments = sum(h_double);
                report("CPU double (renderer path)", t, seconds, cfg);
            }
            if (cfg.cpu_float) {
                histogram_type raw(bins);
                totals t;
                const double seconds = run_cpu_float(cfg, p, core.data.data(), raw, t);
                h_float = snapshot(raw);
                report("CPU float (GPU algorithm)", t, seconds, cfg);
            }
            const std::string device = default_device().name.UTF8String;
            if (cfg.gpu_simple) {
                totals t;
                const double seconds = run_gpu_simple(cfg, p, core.data, h_simple, t);
                report("Metal float, one sample per thread (" + device + ")", t, seconds, cfg);
            }
            if (cfg.gpu_persistent) {
                totals t;
                const double seconds = run_gpu_persistent(cfg, p, core.data, h_persistent, t);
                report("Metal float, persistent threads (" + device + ")", t, seconds, cfg);
            }

            std::cout << "histogram agreement:\n";
            compare("CPU double vs Metal persistent", h_double, h_persistent, s.w);
            compare("CPU float vs Metal persistent", h_float, h_persistent, s.w);
            compare("Metal simple vs Metal persistent", h_simple, h_persistent, s.w);
            compare("CPU double vs CPU float", h_double, h_float, s.w);
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "metal-render-benchmark: " << error.what() << "\n";
            return 1;
        }
    }
}
