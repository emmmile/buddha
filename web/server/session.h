#pragma once
#include "sampler.h"
#include "image_pipeline.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace buddha_browser {

using buddha_image::adjustments;

constexpr uint32_t batch_samples = 1U << 27;

inline double seconds_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}

// Progress and timing counters, copied from the session to the status response in one piece.
struct session_stats {
    uint64_t samples = 0;
    double elapsed = 0;
    double batch_seconds = 0;   // GPU execution time of completed batches
    double capture_seconds = 0; // histogram copies; the GPU waits for these
    uint64_t capture_count = 0;
    double preview_seconds = 0; // frames from a new capture, built while the next batch runs
    uint64_t preview_count = 0;
    double recolor_seconds = 0; // display-only frames from the cached float image
    uint64_t recolor_count = 0;
};

struct render_session {
    uint64_t id;
    std::unique_ptr<buddha_browser::sampler> sampler;
    session_stats stats;
    std::chrono::steady_clock::time_point start, last_capture;
    double elapsed_before_pause = 0;
    bool paused = false;

    buddha_image::tone_mapper tone;
    std::vector<uint32_t> snapshot;
    std::optional<uint64_t> snapshot_samples; // stats.samples when snapshot was captured
    std::optional<uint64_t> base_samples;     // snapshot_samples when base was tone-mapped
    buddha_image::float_image base, adjusted;
    buddha_image::adjustment_buffers buffers;

    render_session(uint64_t render_id, const request_settings &c)
        : id(render_id), sampler(buddha_browser::make_metal_sampler(c)), tone(sampler->image().s) {
        start = last_capture = std::chrono::steady_clock::now();
    }

    double elapsed() const { return elapsed_before_pause + (paused ? 0 : seconds_since(start)); }

    session_stats current_stats() const {
        session_stats current = stats;
        current.elapsed = elapsed();
        return current;
    }

    void pause() {
        if (!paused) {
            elapsed_before_pause = elapsed();
            paused = true;
        }
    }

    void resume() {
        if (paused) {
            start = std::chrono::steady_clock::now();
            paused = false;
        }
    }

    void dispatch(std::random_device &random) {
        sampler->dispatch(batch_samples, random(), random());
    }

    // A completed batch is the only safe point for reading the histogram.
    void finish() {
        stats.batch_seconds += sampler->finish();
        stats.samples += batch_samples;
    }

    // Copies the histogram between batches. The copy, not the conversion, is the only preview
    // work the GPU waits for.
    void capture() {
        if (snapshot_samples == stats.samples)
            return;
        const auto begin = std::chrono::steady_clock::now();
        buddha_image::capture(sampler->image().raw, snapshot);
        snapshot_samples = stats.samples;
        stats.capture_seconds += seconds_since(begin);
        ++stats.capture_count;
        last_capture = begin;
    }

    bool has_unconverted_capture() const { return snapshot_samples != base_samples; }

    // Builds a frame from the latest capture. It reads only the snapshot, so it may run while a
    // batch writes the histogram.
    std::shared_ptr<const std::vector<uint8_t>> frame(const adjustments &display) {
        const auto begin = std::chrono::steady_clock::now();
        const bool new_histogram = has_unconverted_capture();
        if (new_histogram) {
            tone.map(snapshot, sampler->image().s, base);
            base_samples = snapshot_samples;
        }
        buddha_image::adjust(base, display, adjusted, buffers);
        auto rgba = std::make_shared<std::vector<uint8_t>>();
        buddha_image::to_rgba8(adjusted, *rgba);
        const double seconds = seconds_since(begin);
        if (new_histogram) {
            stats.preview_seconds += seconds;
            ++stats.preview_count;
        } else {
            stats.recolor_seconds += seconds;
            ++stats.recolor_count;
        }
        return rgba;
    }
};

struct state {
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<request_settings> pending;
    bool stop = false;
    bool pause = false;
    bool resume = false;
    bool shutdown = false;
    uint64_t render_id = 0;
    uint64_t frame_render_id = 0;
    uint64_t frame_revision = 0;
    uint64_t frame_samples = 0;
    uint64_t display_revision = 0;
    uint64_t frame_display_revision = 0;
    adjustments display;
    session_stats stats;
    double cre = 0, cim = 0, scale = 0;
    uint32_t output_width = 0, output_height = 0;
    std::string phase = "idle";
    std::string sampler_name = "";
    std::string error;
    std::shared_ptr<const std::vector<uint8_t>> frame;
    std::chrono::steady_clock::time_point client_seen; // last /status or /frame.rgba request
};

// The page polls once a second. Without a recent poll, no one is watching, so the worker builds
// no frames; the next poll wakes it and a frame follows within about two batches.
constexpr auto client_timeout = std::chrono::seconds(3);

// Call with the mutex held.
inline bool client_active(const state &shared) {
    return std::chrono::steady_clock::now() - shared.client_seen < client_timeout;
}

inline void note_client(state &shared) {
    bool returned = false;
    {
        std::lock_guard lock(shared.mutex);
        returned = !client_active(shared);
        shared.client_seen = std::chrono::steady_clock::now();
    }
    if (returned)
        shared.changed.notify_one();
}

// Call with the mutex held: a client is watching, and the published frame lacks the session's
// latest completed batch or the current display settings.
inline bool needs_frame(const state &shared, const render_session &session) {
    return shared.render_id == session.id && !shared.pending && client_active(shared) &&
           (shared.frame_render_id != session.id || shared.frame_samples != session.stats.samples ||
            shared.frame_display_revision != shared.display_revision);
}

inline void publish_frame(state &shared, render_session &session) {
    adjustments display;
    uint64_t display_revision = 0;
    {
        std::lock_guard lock(shared.mutex);
        if (shared.render_id != session.id || shared.pending)
            return;
        display = shared.display;
        display_revision = shared.display_revision;
    }
    auto frame = session.frame(display);
    std::lock_guard lock(shared.mutex);
    if (shared.render_id == session.id && !shared.pending &&
        shared.display_revision == display_revision) {
        shared.frame = std::move(frame);
        shared.frame_render_id = session.id;
        shared.frame_samples = *session.base_samples;
        shared.frame_display_revision = display_revision;
        ++shared.frame_revision;
        shared.stats = session.current_stats();
    }
}

inline void render_worker(state &shared) {
    std::optional<render_session> session;
    std::random_device random;
    bool running = false;
    while (true) {
        std::optional<request_settings> next;
        uint64_t next_id = 0;
        bool refresh = false;
        {
            std::unique_lock lock(shared.mutex);
            if (!running)
                shared.changed.wait(lock, [&] {
                    return shared.shutdown || shared.pending.has_value() || shared.stop ||
                           shared.pause || shared.resume ||
                           (session && needs_frame(shared, *session));
                });
            if (shared.shutdown)
                return;
            if (shared.pending) {
                next = std::move(shared.pending);
                shared.pending.reset();
                next_id = shared.render_id;
                running = false;
            } else {
                if (shared.stop || shared.pause) {
                    shared.phase = shared.stop ? "stopped" : "paused";
                    shared.stop = shared.pause = shared.resume = false;
                    running = false;
                    if (session) {
                        session->pause();
                        shared.stats = session->current_stats();
                    }
                } else if (shared.resume) {
                    shared.resume = false;
                    if (session && shared.render_id == session->id) {
                        session->resume();
                        running = true;
                        shared.phase = "running";
                    }
                }
                refresh = !running && session && needs_frame(shared, *session);
            }
        }
        if (next) {
            session.reset();
            try {
                session.emplace(next_id, *next);
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == next_id) {
                    shared.phase = "running";
                    shared.sampler_name = std::string(session->sampler->name());
                    running = true;
                }
            } catch (const std::exception &e) {
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == next_id) {
                    shared.phase = "error";
                    shared.error = e.what();
                    shared.pause = false;
                    shared.resume = false;
                }
                session.reset();
            }
            continue;
        }
        if (!session || (!running && !refresh))
            continue;

        try {
            {
                if (!running) {
                    // Paused or stopped: the GPU is idle, so capture and convert directly.
                    session->capture();
                    publish_frame(shared, *session);
                } else {
                    session->dispatch(random);
                    if (session->has_unconverted_capture())
                        publish_frame(shared, *session); // overlaps with the batch
                    session->finish();
                    bool capture = false;
                    {
                        std::lock_guard lock(shared.mutex);
                        if (shared.render_id == session->id && !shared.pending) {
                            shared.stats = session->current_stats();
                            // A pause or stop captures after the worker has seen it, above.
                            // A changed display revision also captures the completed batch;
                            // the next iteration publishes it even if the old snapshot was
                            // already converted.
                            capture = !shared.stop && !shared.pause && client_active(shared) &&
                                      (shared.frame_render_id != session->id ||
                                       shared.frame_display_revision != shared.display_revision ||
                                       std::chrono::steady_clock::now() - session->last_capture >=
                                           std::chrono::seconds(1));
                        }
                    }
                    if (capture)
                        session->capture();
                }
            }
        } catch (const std::exception &e) {
            {
                std::lock_guard lock(shared.mutex);
                if (shared.render_id == session->id) {
                    shared.phase = "error";
                    shared.error = e.what();
                    shared.pause = false;
                    shared.resume = false;
                }
            }
            session.reset();
            running = false;
        }
    }
}

} // namespace buddha_browser
