// Exercise the plain C++ worker without Metal or HTTP.
#include "session.h"
#include <cstdlib>
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "failed: " #condition << "\n";                                            \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#include <functional>
#include <iostream>

namespace buddha_browser {
class fake_sampler final : public sampler {
    buddha image_;
    bool in_flight_ = false;

  public:
    explicit fake_sampler(const request_settings &request)
        : image_([&] {
              settings s = make_settings(request);
              s.exclusion.clear();
              return s;
          }()) {}
    void dispatch(uint32_t, uint32_t, uint32_t) override {
        CHECK(!in_flight_);
        in_flight_ = true;
    }
    double finish() override {
        CHECK(in_flight_);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        in_flight_ = false;
        image_.raw[0].store(image_.raw[0].load() + 1);
        return 0.002;
    }
    const buddha &image() const override { return image_; }
    std::string_view name() const override { return "fake"; }
};
std::unique_ptr<sampler> make_metal_sampler(const request_settings &request) {
    return std::make_unique<fake_sampler>(request);
}
} // namespace buddha_browser

using namespace buddha_browser;

bool until(state &shared, const std::function<bool(const state &)> &predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        note_client(shared);
        {
            std::lock_guard lock(shared.mutex);
            if (shared.phase == "error") {
                std::cerr << shared.error << '\n';
                return false;
            }
            if (predicate(shared))
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

int main() {
    state shared;
    std::thread worker(render_worker, std::ref(shared));
    const request_settings request{24, 16, -0.5, 0, 6, 0, 0, 0, 8, 8, 8};
    auto start = [&](uint64_t id) {
        std::lock_guard lock(shared.mutex);
        shared.render_id = id;
        shared.pending = request;
        shared.phase = "starting";
        shared.changed.notify_one();
    };
    start(1);
    CHECK(until(shared,
                [](const state &s) { return s.frame_render_id == 1 && s.stats.samples > 0; }));
    // A display change during sampling must publish a new frame even when the previous
    // histogram snapshot has already been converted.
    uint64_t running_display_revision;
    {
        std::lock_guard lock(shared.mutex);
        shared.display.contrast = 30;
        running_display_revision = ++shared.display_revision;
        shared.changed.notify_one();
    }
    CHECK(until(shared, [&](const state &s) {
        return s.phase == "running" && s.frame_display_revision == running_display_revision;
    }));
    {
        std::lock_guard lock(shared.mutex);
        shared.pause = true;
        shared.changed.notify_one();
    }
    CHECK(until(shared, [](const state &s) {
        return s.phase == "paused" && s.frame_samples == s.stats.samples;
    }));
    uint64_t paused_samples, preview_count;
    {
        std::lock_guard lock(shared.mutex);
        paused_samples = shared.stats.samples;
        preview_count = shared.stats.preview_count;
        shared.display.brightness = 40;
        ++shared.display_revision;
        shared.changed.notify_one();
    }
    CHECK(until(shared,
                [](const state &s) { return s.frame_display_revision == s.display_revision; }));
    {
        std::lock_guard lock(shared.mutex);
        CHECK(shared.stats.samples == paused_samples);
        CHECK(shared.stats.preview_count == preview_count);
        CHECK(shared.stats.recolor_count > 0);
        shared.resume = true;
        shared.changed.notify_one();
    }
    CHECK(until(shared, [&](const state &s) { return s.stats.samples > paused_samples; }));
    {
        std::lock_guard lock(shared.mutex);
        shared.stop = true;
        shared.changed.notify_one();
    }
    CHECK(until(shared, [](const state &s) {
        return s.phase == "stopped" && s.frame_samples == s.stats.samples;
    }));
    start(2);
    CHECK(until(shared, [](const state &s) { return s.frame_render_id == 2; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::lock_guard lock(shared.mutex);
        CHECK(shared.frame_revision > 0 && shared.frame_render_id == shared.render_id);
        shared.shutdown = true;
        shared.changed.notify_one();
    }
    worker.join();
    std::cout << "ok\n";
}
