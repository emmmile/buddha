#include "buddha_generator.h"
#include "orbit_reference.h"

#include <boost/log/core.hpp>
#include <boost/program_options.hpp>
#include <algorithm>
#include <bit>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>

namespace {
using complex_type = buddha::complex_type;
namespace po = boost::program_options;

struct options {
    settings s{};
    std::string mode = "orbit", kernel = "production", corpus = "mixed";
    uint64_t seed = 1;
    unsigned int chains = 16, samples = 4096, rounds = 4, proposal_limit = 16384, repeats = 5;
};

bool finite_setting(double value) {
    // std::isfinite can be folded to true under this project's -ffast-math.
    return (std::bit_cast<uint64_t>(value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

bool parse(int argc, char **argv, options &o) {
    auto &s = o.s;
    po::options_description desc("Buddha++ fixed-work benchmark (never writes render files)");
    desc.add_options()("help", "show options")("mode", po::value(&o.mode)->default_value(o.mode),
                                               "orbit, histogram, or generator")(
        "kernel", po::value(&o.kernel)->default_value(o.kernel),
        "orbit only: production or reference")("corpus",
                                               po::value(&o.corpus)->default_value(o.corpus),
                                               "orbit/histogram inputs: uniform, boundary, mixed")(
        "seed", po::value(&o.seed)->default_value(o.seed),
        "benchmark seed (not a renderer option)")("chains",
                                                  po::value(&o.chains)->default_value(o.chains),
                                                  "logical chains, independent of thread count")(
        "threads", po::value(&s.threads)->default_value(1),
        "worker threads")("samples", po::value(&o.samples)->default_value(o.samples),
                          "input orbits per chain in orbit/histogram modes")(
        "rounds", po::value(&o.rounds)->default_value(o.rounds),
        "corpus replays or bounded Metropolis segments per chain")(
        "proposal-limit", po::value(&o.proposal_limit)->default_value(o.proposal_limit),
        "maximum proposals per Metropolis segment")("repeats",
                                                    po::value(&o.repeats)->default_value(o.repeats),
                                                    "measured repetitions after one warmup")(
        "width", po::value(&s.w)->default_value(1024),
        "image width")("height", po::value(&s.h)->default_value(1024), "image height")(
        "scale", po::value(&s.scale)->default_value(256.0), "pixels per complex unit")(
        "cre", po::value(&s.cre)->default_value(0.0),
        "view real center")("cim", po::value(&s.cim)->default_value(0.0), "view imaginary center")(
        "red-min", po::value(&s.lowr)->default_value(512), "red lower iteration threshold")(
        "green-min", po::value(&s.lowg)->default_value(128), "green lower iteration threshold")(
        "blue-min", po::value(&s.lowb)->default_value(32), "blue lower iteration threshold")(
        "red-max", po::value(&s.highr)->default_value(8192), "red upper iteration threshold")(
        "green-max", po::value(&s.highg)->default_value(2048), "green upper iteration threshold")(
        "blue-max", po::value(&s.highb)->default_value(512), "blue upper iteration threshold")(
        "exclusion-map", po::value(&s.exclusion)->default_value(""),
        "existing map; empty disables exclusions")(
        "exclusion-size", po::value(&s.exclusion_size)->default_value(4096),
        "existing exclusion-map side length");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << '\n';
        return false;
    }
    if (o.mode != "orbit" && o.mode != "histogram" && o.mode != "generator")
        throw std::invalid_argument("unknown mode: " + o.mode);
    if (o.kernel != "production" && o.kernel != "reference")
        throw std::invalid_argument("unknown kernel: " + o.kernel);
    if (o.kernel != "production" && o.mode != "orbit")
        throw std::invalid_argument("--kernel only applies to orbit mode");
    if (o.corpus != "uniform" && o.corpus != "boundary" && o.corpus != "mixed")
        throw std::invalid_argument("unknown corpus: " + o.corpus);
    if (!o.chains || !s.threads || !o.samples || !o.rounds || !o.proposal_limit || !o.repeats)
        throw std::invalid_argument("work counts and thread counts must be positive");
    if (!s.w || !s.h || !finite_setting(s.scale) || !finite_setting(s.cre) ||
        !finite_setting(s.cim) || !(s.scale > 0) ||
        s.w > std::numeric_limits<size_t>::max() / sizeof(buddha::pixel) / 3 / s.h)
        throw std::invalid_argument("invalid geometry or histogram size overflow");
    if (s.lowr >= s.highr || s.lowg >= s.highg || s.lowb >= s.highb ||
        std::max({s.highr, s.highg, s.highb}) > std::numeric_limits<int>::max() / 256)
        throw std::invalid_argument("invalid iteration ranges");
    if (!s.exclusion_size || s.exclusion_size % 2 || s.exclusion_size > 65536)
        throw std::invalid_argument("exclusion size must be even and between 2 and 65536");
    s.contrast = s.lightness = 100;
    s.no_image = true;
    s.indirect_settings();
    if (!finite_setting(s.minre) || !finite_setting(s.maxre) || !finite_setting(s.minim) ||
        !finite_setting(s.maxim) || !finite_setting(20.0 / s.scale))
        throw std::invalid_argument("geometry exceeds the floating-point range");
    return true;
}

uint64_t chain_seed(uint64_t seed, uint64_t chain) {
    // SplitMix64: each logical chain has a stream independent of its worker.
    uint64_t z = seed + 0x9e3779b97f4a7c15ULL * (chain + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void hash_value(uint64_t &hash, uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; }

struct counters {
    uint64_t evaluated = 0, orbits = 0, escaped = 0, contributions = 0;
    uint64_t proposals = 0, accepted = 0, drawn = 0, find_attempts = 0;
    uint64_t checksum = 14695981039346656037ULL;
};

struct draw_command {
    complex_type point;
    unsigned char mask;
};

struct chain {
    std::vector<complex_type> inputs, seq;
    std::vector<draw_command> replay;
    std::unique_ptr<buddha_generator> generator;
    counters result;
};

std::vector<complex_type> inputs(const options &o, unsigned int id) {
    static const complex_type boundary[] = {{-0.148409, 0.835417},   {0.292942, 0.450511},
                                            {-0.767293, -0.0913622}, {-0.328038, -0.613444},
                                            {-0.357572, -0.601857},  {-0.261254, 0.633443},
                                            {-0.0909596, 0.648495},  {-0.749504, -0.0253876}};
    std::mt19937_64 random(chain_seed(o.seed, id));
    std::uniform_real_distribution<double> uniform(-2.0, 2.0);
    std::vector<complex_type> out;
    out.reserve(o.samples);
    for (unsigned int i = 0; i < o.samples; ++i) {
        const double re = uniform(random);
        const double im = uniform(random);
        const complex_type offset(re, im);
        if (o.corpus == "boundary" || (o.corpus == "mixed" && i % 2))
            out.push_back(boundary[(i / 2) % std::size(boundary)] + offset * 0.00001);
        else
            out.push_back(offset);
    }
    return out;
}

// Keep sequence stores observable with LTO: time an evaluator that records an
// orbit, not a dead-store-eliminated escape test.
template <class Core>
[[gnu::noinline]] int evaluate(const Core &core, std::vector<complex_type> &seq,
                               unsigned int &contribute, unsigned int &calculated) {
    return core.evaluate(seq, contribute, calculated);
}

template <class Core>
void orbit_work(chain &c, const Core &core, const options &o, const mandelbrot<complex_type> &map) {
    for (unsigned int round = 0; round < o.rounds; ++round) {
        for (auto point : c.inputs) {
            c.seq[0] = point;
            unsigned int contribute = 0, calculated = 0;
            int end = -1;
            if (!map.excluded(point))
                end = evaluate(core, c.seq, contribute, calculated);
            c.result.evaluated += calculated;
            ++c.result.orbits;
            c.result.escaped += end >= 0;
            c.result.contributions += contribute;
            hash_value(c.result.checksum, static_cast<uint64_t>(end));
            hash_value(c.result.checksum, calculated);
            hash_value(c.result.checksum, contribute);
            hash_value(c.result.checksum, std::bit_cast<uint64_t>(c.seq[calculated].real()));
            hash_value(c.result.checksum, std::bit_cast<uint64_t>(c.seq[calculated].imag()));
        }
    }
}

template <class Work>
void parallel_chains(std::vector<chain> &chains, unsigned int threads, Work work) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> workers;
    for (size_t t = 0; t < std::min<size_t>(threads, chains.size()); ++t)
        workers.emplace_back([&] {
            for (size_t id = next.fetch_add(1); id < chains.size(); id = next.fetch_add(1))
                work(chains[id]);
        });
    for (auto &worker : workers)
        worker.join();
}

int run(const options &o) {
    const auto &s = o.s;
    mandelbrot<complex_type> core(s);
    if (!s.exclusion.empty() && !core.load())
        throw std::runtime_error("could not load requested exclusion map");
    buddha::vector_type raw(o.mode == "orbit" ? 0 : 3 * s.size);
    std::vector<chain> chains(o.chains);
    for (unsigned int id = 0; id < o.chains; ++id) {
        auto &c = chains[id];
        if (o.mode != "generator") {
            c.seq.resize(static_cast<size_t>(s.high) + 1);
            c.inputs = inputs(o, id);
        }
        if (o.mode == "histogram") {
            for (auto point : c.inputs) {
                c.seq[0] = point;
                unsigned int contribute = 0, calculated = 0;
                const int end = core.evaluate(c.seq, contribute, calculated);
                if (end <= 0 || contribute == 0)
                    continue;
                for (unsigned int i = s.low; i <= static_cast<unsigned int>(end) && i < s.high;
                     ++i) {
                    const auto mask = static_cast<unsigned char>(
                        (i > s.lowr && i < s.highr ? 1 : 0) | (i > s.lowg && i < s.highg ? 2 : 0) |
                        (i > s.lowb && i < s.highb ? 4 : 0));
                    c.replay.push_back({c.seq[i], mask});
                }
            }
        }
    }
    size_t replay_bytes = 0;
    for (const auto &c : chains)
        replay_bytes += c.replay.capacity() * sizeof(draw_command);
    std::cout << "mode=" << o.mode << " kernel=" << o.kernel << " corpus=" << o.corpus
              << " seed=" << o.seed << " chains=" << o.chains << " threads=" << s.threads
              << " samples=" << o.samples << " rounds=" << o.rounds
              << " proposal_limit=" << o.proposal_limit << " repeats=" << o.repeats
              << " width=" << s.w << " height=" << s.h << " scale=" << s.scale << " cre=" << s.cre
              << " cim=" << s.cim << " rgb=" << s.lowr << ':' << s.highr << ',' << s.lowg << ':'
              << s.highg << ',' << s.lowb << ':' << s.highb
              << " exclusion=" << (s.exclusion.empty() ? "disabled" : s.exclusion)
              << " histogram_bytes=" << raw.size() * sizeof(buddha::vector_type::value_type)
              << " replay_bytes=" << replay_bytes << '\n';
    std::vector<double> times;
    std::string expected;
    for (uint64_t repetition = 0; repetition <= o.repeats; ++repetition) {
        for (auto &pixel : raw)
            pixel.store(0);
        for (unsigned int id = 0; id < o.chains; ++id) {
            chains[id].result = {};
            if (o.mode != "orbit")
                chains[id].generator =
                    std::make_unique<buddha_generator>(core, raw, s, chain_seed(o.seed, id));
        }
        timer clock;
        parallel_chains(chains, s.threads, [&](chain &c) {
            if (o.mode == "orbit") {
                if (o.kernel == "reference")
                    orbit_work(c, orbit_reference<complex_type>{s}, o, core);
                else
                    orbit_work(c, static_cast<const mandelbrot_base<complex_type> &>(core), o,
                               core);
            } else if (o.mode == "histogram") {
                for (unsigned int round = 0; round < o.rounds; ++round)
                    for (auto &command : c.replay)
                        c.generator->drawPoint(command.point, command.mask & 1, command.mask & 2,
                                               command.mask & 4);
                c.result.drawn = c.replay.size() * o.rounds;
            } else {
                for (unsigned int round = 0; round < o.rounds; ++round)
                    c.generator->metropolis(o.proposal_limit);
                c.result.evaluated = c.generator->computed;
                c.result.proposals = c.generator->proposals;
                c.result.accepted = c.generator->accepted;
                c.result.drawn = c.generator->drawn_orbits;
                c.result.find_attempts = c.generator->find_attempts;
            }
        });
        const double seconds = clock.elapsed();
        counters total;
        for (const auto &c : chains) {
            total.evaluated += c.result.evaluated;
            total.orbits += c.result.orbits;
            total.escaped += c.result.escaped;
            total.contributions += c.result.contributions;
            total.proposals += c.result.proposals;
            total.accepted += c.result.accepted;
            total.drawn += c.result.drawn;
            total.find_attempts += c.result.find_attempts;
            hash_value(total.checksum, c.result.checksum);
        }
        uint64_t increments = 0, histogram_hash = 14695981039346656037ULL;
        for (const auto &pixel : raw) {
            const auto value = pixel.load();
            increments += value;
            hash_value(histogram_hash, value);
        }
        std::ostringstream signature;
        signature << "evaluated=" << total.evaluated << " orbits=" << total.orbits
                  << " escaped=" << total.escaped << " contributions=" << total.contributions
                  << " proposals=" << total.proposals << " accepted=" << total.accepted
                  << " drawn=" << total.drawn << " find_attempts=" << total.find_attempts
                  << " increments=" << increments << " orbit_checksum=" << total.checksum
                  << " histogram_checksum=" << histogram_hash;
        if (repetition == 0) {
            expected = signature.str();
            std::cout << "warmup " << expected << '\n';
        } else {
            if (signature.str() != expected)
                throw std::runtime_error("fixed-work results changed between repetitions");
            times.push_back(seconds);
            std::cout << "repeat=" << repetition << " seconds=" << seconds
                      << " evaluated_per_s=" << total.evaluated / seconds
                      << " increments_per_s=" << increments / seconds << '\n';
        }
    }
    std::sort(times.begin(), times.end());
    const double median = (times[(times.size() - 1) / 2] + times[times.size() / 2]) / 2;
    std::cout << "median_seconds=" << median << " min_seconds=" << times.front()
              << " max_seconds=" << times.back();
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        const uint64_t peak_bytes = usage.ru_maxrss;
#else
        const uint64_t peak_bytes = static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
        std::cout << " peak_rss_bytes=" << peak_bytes;
    }
    std::cout << '\n';
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        boost::log::core::get()->set_logging_enabled(false);
        std::cout << std::setprecision(9);
        options o;
        return parse(argc, argv, o) ? run(o) : 0;
    } catch (const std::exception &error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}
