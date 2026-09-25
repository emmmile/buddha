#include "buddha_generator.h"
#include "orbit_reference.h"

#include <boost/log/core.hpp>
#include <bit>
#include <stdexcept>

namespace {
using C = buddha::complex_type;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

settings make_settings() {
    settings s{};
    s.w = s.h = 1024;
    s.scale = 256;
    s.highr = 8192;
    s.highg = 2048;
    s.highb = 512;
    s.lowr = 512;
    s.lowg = 128;
    s.lowb = 32;
    s.exclusion_size = 16;
    s.indirect_settings();
    return s;
}

template <class Core>
[[gnu::noinline]] int evaluate(const Core &core, std::vector<C> &seq, unsigned int &contribute,
                               unsigned int &calculated) {
    return core.evaluate(seq, contribute, calculated);
}

void compare(const settings &s, const std::vector<C> &points) {
    orbit_reference<C> reference{s};
    mandelbrot<C> production(s);
    std::vector<C> a(s.high + 1), b(s.high + 1);
    for (auto point : points) {
        a[0] = b[0] = point;
        unsigned int ac, ai, bc, bi;
        const int ae = evaluate(reference, a, ac, ai);
        const int be = evaluate(production, b, bc, bi);
        require(ae == be && ac == bc && ai == bi, "orbit metadata differs from reference");
        for (unsigned int i = 0; i <= ai; ++i) {
            if (std::bit_cast<uint64_t>(a[i].real()) != std::bit_cast<uint64_t>(b[i].real()) ||
                std::bit_cast<uint64_t>(a[i].imag()) != std::bit_cast<uint64_t>(b[i].imag())) {
                std::cerr << std::hexfloat << "c=" << point << " high=" << s.high << " i=" << i
                          << " reference=" << a[i] << " production=" << b[i] << '\n';
                throw std::runtime_error("orbit coordinates differ from reference");
            }
        }
    }
}

void orbit_test() {
    std::vector<C> points{{0, 0},
                          {-1, 0},
                          {-2, 0},
                          {2, 0},
                          {3, 0},
                          {0, 3},
                          {-0.148409, 0.835417},
                          {0.292942, 0.450511},
                          {-0.767293, -0.0913622},
                          {-0.749504, -0.0253876}};
    std::mt19937_64 random(7);
    std::uniform_real_distribution<double> uniform(-2, 2);
    for (unsigned int i = 0; i < 8192; ++i) {
        const double re = uniform(random), im = uniform(random);
        points.emplace_back(re, im);
        points.push_back(C(-0.148409, 0.835417) + C(re, im) * 0.00001);
    }
    for (auto high : {1u, 8u, 9u, 16u, 17u, 512u, 8192u}) {
        auto s = make_settings();
        s.high = high;
        compare(s, points);
    }
    for (auto center : {C(0, 0), C(-0.5, 0), C(-0.5, 0.6), C(-0.5, -0.6)}) {
        auto s = make_settings();
        s.h = 1023;
        s.scale = 2048;
        s.cre = center.real();
        s.cim = center.imag();
        s.indirect_settings();
        auto boundaries = points;
        for (double x : {s.minre, s.maxre})
            for (double y : {s.minim, s.maxim, 0.0}) {
                boundaries.emplace_back(x, y);
                const double limit = std::numeric_limits<double>::max();
                boundaries.emplace_back(std::nextafter(x, -limit), y);
                boundaries.emplace_back(std::nextafter(x, limit), y);
                boundaries.emplace_back(x, std::nextafter(y, -limit));
                boundaries.emplace_back(x, std::nextafter(y, limit));
            }
        compare(s, boundaries);
    }
}

void accounting_test() {
    auto s = make_settings();
    s.lowr = s.lowg = s.lowb = 0;
    s.highr = s.highg = s.highb = 1;
    s.indirect_settings();
    mandelbrot<C> core(s);
    buddha::vector_type raw(3 * s.size);
    buddha_generator generator(core, raw, s, 1);
    generator.metropolis(5);
    require(generator.proposals == 5 && generator.drawn_orbits == 0,
            "fixture must evaluate five nonescaping proposals");
    require(generator.computed == generator.find_attempts + generator.proposals,
            "discarded proposals must count toward computed work");

    std::fill(core.data.begin(), core.data.end(), 1);
    std::vector<C> seq(s.high + 1);
    seq[0] = C(0, 0.5);
    unsigned int contribute = 123, calculated = 456;
    require(core.evaluate(seq, contribute, calculated) == -1, "fixture must be excluded");
    require(contribute == 0 && calculated == 0, "excluded orbit must clear both output counters");
}
} // namespace

int main() {
    boost::log::core::get()->set_logging_enabled(false);
    orbit_test();
    accounting_test();
}
