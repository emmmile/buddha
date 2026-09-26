// buddha-metal: Apple-silicon GPU renderer using naive (uniform) sampling.
//
// It accepts the same options as buddha++ and reuses its exclusion map, checkpoint format and
// TIFF output, but samples starting points uniformly over [-2, 2]^2 instead of running
// Metropolis chains, in single precision. See metal/README.md.

#include "persistent_renderer.h"

#include "buddha.h"
#include "settings_parser.h"
#include "timer.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <deque>
#include <iostream>
#include <random>
#include <thread>

namespace {

using buddha_metal::persistent_renderer;

// A valid map always excludes some interior points; an all-zero map means none was loaded.
bool map_loaded(const mandelbrot<buddha::complex_type> &core) {
    return std::any_of(core.data.begin(), core.data.end(), [](uint8_t v) { return v != 0; });
}

void ensure_exclusion_map(buddha &b) {
    if (b.s.exclusion.empty() || map_loaded(b.core))
        return;
    const bool exists = boost::filesystem::exists(b.s.exclusion);
    BOOST_LOG_TRIVIAL(info) << "generating " << b.s.exclusion_size << "x" << b.s.exclusion_size
                            << " exclusion map";
    timer time;
    b.core.exclusion();
    BOOST_LOG_TRIVIAL(info) << "generated exclusion map in " << time.elapsed() << " s";
    // Never overwrite an existing file, e.g. a map saved with another size.
    if (!exists)
        b.core.save();
    else
        BOOST_LOG_TRIVIAL(warning)
            << "'" << b.s.exclusion << "' could not be loaded; using the generated map unsaved";
}

// Joins the signal waiter on every exit path; if no signal arrived yet, wakes it with one.
struct waiter_guard {
    std::thread &waiter;
    std::atomic<bool> &stop;
    ~waiter_guard() {
        if (!waiter.joinable())
            return;
        if (!stop)
            pthread_kill(waiter.native_handle(), SIGTERM);
        waiter.join();
    }
};

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            // Block the shutdown signals before Metal or any worker thread starts, so only the
            // waiter below receives them (as buddha::run does).
            sigset_t shutdown;
            sigemptyset(&shutdown);
            sigaddset(&shutdown, SIGINT);
            sigaddset(&shutdown, SIGQUIT);
            sigaddset(&shutdown, SIGTERM);
            pthread_sigmask(SIG_BLOCK, &shutdown, nullptr);

            settings_parser parser(argc, argv);
            buddha b(parser());
            const settings &s = b.s;
            if (b.raw.size() > UINT32_MAX)
                throw std::runtime_error("histogram too large for the Metal kernel's 32-bit "
                                         "indices; reduce the image size");
            ensure_exclusion_map(b);

            id<MTLDevice> device = buddha_metal::default_device();
            if (b.raw.size() * sizeof(uint32_t) > device.maxBufferLength)
                throw std::runtime_error("histogram exceeds the Metal device's maximum buffer "
                                         "length");
            persistent_renderer gpu(device, buddha_metal::make_parameters(s), b.core.data.data(),
                                    b.core.data.size(), b.raw.size());
            BOOST_LOG_TRIVIAL(info)
                << "buddha-metal: naive float sampling on " << device.name.UTF8String << ", "
                << gpu.threads() << " GPU threads, " << (gpu.bins() * sizeof(uint32_t) >> 20)
                << " MiB histogram";

            // Continue a loaded checkpoint.
            uint32_t *histogram = gpu.histogram();
            for (uint64_t i = 0; i < b.raw.size(); ++i)
                histogram[i] = b.raw[i].load();

            std::atomic<bool> stop{false};
            std::thread waiter([&] {
                int sig = 0;
                sigwait(&shutdown, &sig);
                BOOST_LOG_TRIVIAL(debug) << "interrupt signal (" << sig << ") received";
                stop = true;
            });
            waiter_guard guard{waiter, stop};

            // Each dispatch draws a fresh random stream; keep two queued so the GPU never idles
            // between them.
            std::random_device random;
            std::deque<id<MTLCommandBuffer>> in_flight;
            uint64_t samples = 0;
            timer time;
            while (!stop) {
                while (in_flight.size() < 2) {
                    in_flight.push_back(
                        gpu.dispatch(0, persistent_renderer::default_batch, random(), random()));
                    samples += persistent_renderer::default_batch;
                }
                persistent_renderer::wait(in_flight.front());
                in_flight.pop_front();
            }
            for (id<MTLCommandBuffer> command : in_flight)
                persistent_renderer::wait(command);
            b.totaltime = time.elapsed();

            for (uint64_t i = 0; i < b.raw.size(); ++i)
                b.raw[i].store(histogram[i]);

            const buddha_metal::thread_totals t = gpu.totals();
            b.computed = t.iterations;
            BOOST_LOG_TRIVIAL(info)
                << "samples: " << samples << " (" << samples / b.totaltime / 1e6
                << " M/s), excluded: " << t.excluded << ", periodic or capped: " << t.periodic
                << ", escaped: " << t.escaped << ", redraw steps: " << t.redraw;

            if (getenv("BUDDHA_BENCHMARK_NO_SAVE"))
                b.reduce();
            else
                b.save();
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "buddha-metal: " << error.what() << "\n";
            return 1;
        }
    }
}
