#pragma once
#include "browser_settings.h"
#include "buddha.h"
#include <memory>
#include <string_view>

namespace buddha_browser {
// Totals over a render's finished batches, possibly with part of a running one.
struct sampler_metrics {
    // Common to every sampler.
    uint64_t orbits = 0; // starting points tried, including those in the exclusion map
    uint64_t drawn = 0;  // orbits drawn into the histogram
    uint64_t steps = 0;  // orbit steps, including the steps that draw an orbit
    uint64_t points = 0; // histogram increments
    // Up to two values particular to the sampler; an empty name ends the list. The page shows
    // primary values on its status line and the others only when debugging.
    struct value {
        const char *name = "";
        double amount = 0;
        const char *unit = "";
        bool primary = false;
    };
    value specific[2];
};

// Batches run in dispatch order, and several may be queued. The histogram may be read while
// they run (its counters are atomic); it is exact once every dispatched batch has finished.
// The sampler owns the image and waits for outstanding work before destroying its GPU-visible
// storage.
class sampler {
  public:
    virtual ~sampler() = default;
    virtual void dispatch(uint32_t key0, uint32_t key1) = 0;
    virtual double finish() = 0; // waits for the oldest batch; returns its seconds of sampling
    virtual sampler_metrics metrics() const = 0;
    virtual const buddha &image() const = 0;
    virtual std::string_view name() const = 0;
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request);
} // namespace buddha_browser
