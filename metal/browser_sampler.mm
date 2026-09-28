#include "sampler.h"
#include "persistent_renderer.h"
#include <filesystem>
#include <stdexcept>

namespace buddha_browser {
class metal_sampler final : public sampler {
    std::unique_ptr<buddha> image_;
    std::unique_ptr<buddha_metal::persistent_renderer> gpu_;
    id<MTLCommandBuffer> in_flight_ = nil;

  public:
    explicit metal_sampler(const request_settings &request) {
        if (!std::filesystem::is_regular_file(BUDDHA_EXCLUSION_MAP))
            throw std::runtime_error("default exclusion map is missing: " BUDDHA_EXCLUSION_MAP);
        image_ = std::make_unique<buddha>(make_settings(request));
        buddha_metal::check_histogram_layout<buddha::vector_type>();
        gpu_ = std::make_unique<buddha_metal::persistent_renderer>(
            buddha_metal::default_device(),
            buddha_metal::make_parameters(image_->s, image_->core.size), image_->core.data.data(),
            image_->core.data.size(), image_->raw.data(),
            buddha_metal::histogram_bytes(image_->raw));
    }
    ~metal_sampler() override {
        if (in_flight_)
            [in_flight_ waitUntilCompleted];
    }
    void dispatch(uint32_t samples, uint32_t key0, uint32_t key1) override {
        @autoreleasepool {
            in_flight_ = gpu_->dispatch(0, samples, key0, key1);
        }
    }
    double finish() override {
        if (!in_flight_)
            return 0;
        id<MTLCommandBuffer> command = in_flight_;
        in_flight_ = nil;
        @autoreleasepool {
            buddha_metal::persistent_renderer::wait(command);
            return command.GPUEndTime - command.GPUStartTime;
        }
    }
    const buddha &image() const override { return *image_; }
    std::string_view name() const override { return "naive"; }
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request) {
    @autoreleasepool {
        return std::make_unique<metal_sampler>(request);
    }
}
} // namespace buddha_browser
