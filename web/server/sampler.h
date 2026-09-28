#pragma once
#include "browser_settings.h"
#include "buddha.h"
#include <memory>
#include <string_view>

namespace buddha_browser {
// Batches run in dispatch order, and several may be queued. The histogram may be read while
// they run (its counters are atomic); it is exact once every dispatched batch has finished.
// The sampler owns the image and waits for outstanding work before destroying its GPU-visible
// storage.
class sampler {
  public:
    virtual ~sampler() = default;
    virtual void dispatch(uint32_t samples, uint32_t key0, uint32_t key1) = 0;
    virtual double finish() = 0; // waits for the oldest batch; returns its seconds of sampling
    virtual const buddha &image() const = 0;
    virtual std::string_view name() const = 0;
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request);
} // namespace buddha_browser
