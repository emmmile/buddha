#pragma once
#include "browser_settings.h"
#include "buddha.h"
#include <memory>
#include <string_view>

namespace buddha_browser {
// A completed wait is the safe point for reading the histogram. The sampler owns the image
// and waits for outstanding work before destroying its GPU-visible storage.
class sampler {
  public:
    virtual ~sampler() = default;
    virtual void dispatch(uint32_t samples, uint32_t key0, uint32_t key1) = 0;
    virtual double finish() = 0; // seconds of sampling in the completed batch
    virtual const buddha &image() const = 0;
    virtual std::string_view name() const = 0;
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request);
} // namespace buddha_browser
