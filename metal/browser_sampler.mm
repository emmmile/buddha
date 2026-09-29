#include "sampler.h"
#include "persistent_renderer.h"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <random>
#include <stdexcept>

namespace buddha_browser {
namespace {
// Naive samples per batch, and Metropolis orbit steps per lane per batch.
constexpr uint32_t naive_batch = 1U << 27;
constexpr uint32_t metropolis_steps = 1U << 14;

buddha_kernel::metropolis_parameters metropolis_parameters(const request_settings &request) {
    const metropolis_settings &m = request.metropolis;
    buddha_kernel::metropolis_parameters p{};
    const double view = std::min(request.output_width, request.output_height) / request.scale;
    p.radius = float(m.radius / 100 * view);
    p.exponent_l = float(m.exponent_l);
    p.exponent_c = float(m.exponent_c);
    p.chain_scale = float(m.chain_scale);
    p.seeding =
        m.seeding == "uniform" ? buddha_kernel::seeding_uniform : buddha_kernel::seeding_walk;
    p.steps = metropolis_steps;
    return p;
}
} // namespace

class metal_sampler final : public sampler {
    std::unique_ptr<buddha> image_;
    std::unique_ptr<buddha_metal::persistent_renderer> gpu_;
    std::deque<id<MTLCommandBuffer>> in_flight_;
    bool metropolis_;
    buddha_kernel::metropolis_parameters chain_;

  public:
    explicit metal_sampler(const request_settings &request)
        : metropolis_(request.sampler == "metropolis"), chain_(metropolis_parameters(request)) {
        if (!std::filesystem::is_regular_file(BUDDHA_EXCLUSION_MAP))
            throw std::runtime_error("default exclusion map is missing: " BUDDHA_EXCLUSION_MAP);
        image_ = std::make_unique<buddha>(make_settings(request));
        buddha_metal::check_histogram_layout<buddha::vector_type>();
        gpu_ = std::make_unique<buddha_metal::persistent_renderer>(
            buddha_metal::default_device(),
            buddha_metal::make_parameters(image_->s, image_->core.size), image_->core.data.data(),
            image_->core.data.size(), image_->raw.data(),
            buddha_metal::histogram_bytes(image_->raw));
        if (metropolis_)
            gpu_->begin_chains(std::random_device{}());
    }
    ~metal_sampler() override {
        for (id<MTLCommandBuffer> command : in_flight_)
            [command waitUntilCompleted];
    }
    void dispatch(uint32_t key0, uint32_t key1) override {
        @autoreleasepool {
            in_flight_.push_back(metropolis_ ? gpu_->dispatch_metropolis(chain_)
                                             : gpu_->dispatch(0, naive_batch, key0, key1));
        }
    }
    double finish() override {
        if (in_flight_.empty())
            return 0;
        id<MTLCommandBuffer> command = in_flight_.front();
        in_flight_.pop_front();
        @autoreleasepool {
            buddha_metal::persistent_renderer::wait(command);
            return command.GPUEndTime - command.GPUStartTime;
        }
    }
    sampler_metrics metrics() const override {
        const buddha_kernel::totals t = gpu_->sum();
        sampler_metrics m;
        m.drawn = t.escaped;
        m.steps = t.iterations + t.redraw;
        m.points = t.increments;
        const auto share = [](uint64_t part, uint64_t whole) {
            return whole ? 100.0 * double(part) / double(whole) : 0.0;
        };
        if (metropolis_) {
            m.orbits = t.seeds + t.proposals;
            m.specific[0] = {"accepted", share(t.accepted, t.proposals), "%", true};
            m.specific[1] = {"chain", t.chains ? double(t.proposals) / double(t.chains) : 0.0,
                             "proposals", false};
        } else {
            m.orbits = t.escaped + t.periodic + t.excluded;
            m.specific[0] = {"excluded", share(t.excluded, m.orbits), "%", false};
        }
        return m;
    }
    const buddha &image() const override { return *image_; }
    std::string_view name() const override { return metropolis_ ? "metropolis" : "naive"; }
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request) {
    @autoreleasepool {
        return std::make_unique<metal_sampler>(request);
    }
}
} // namespace buddha_browser
