// buddha-metal: Apple-silicon GPU renderer using naive (uniform) sampling.
//
// It is buddha++ with a different generator: options, exclusion map, checkpoints and TIFF output
// all go through the same buddha object, and the GPU renders straight into its histogram.
// Starting points are sampled uniformly over [-2, 2]^2 in single precision instead of running
// Metropolis chains. See metal/README.md.

#include "persistent_renderer.h"

#include "buddha.h"
#include "settings_parser.h"
#include "timer.h"

#include <atomic>
#include <csignal>
#include <deque>
#include <iostream>
#include <random>
#include <thread>

namespace {

using buddha_metal::persistent_renderer;

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

            // Loads the exclusion map and a --load checkpoint exactly as buddha++ does.
            settings_parser parser(argc, argv);
            buddha b(parser());
            buddha_metal::check_histogram_layout<buddha::vector_type>();
            if (b.raw.size() > UINT32_MAX)
                throw std::runtime_error("histogram too large for the Metal kernel's 32-bit "
                                         "indices; reduce the image size");

            id<MTLDevice> device = buddha_metal::default_device();
            persistent_renderer gpu(device, buddha_metal::make_parameters(b.s), b.core.data.data(),
                                    b.core.data.size(), b.raw.data(),
                                    buddha_metal::histogram_bytes(b.raw));
            BOOST_LOG_TRIVIAL(info)
                << "buddha-metal: naive float sampling on " << device.name.UTF8String << ", "
                << gpu.threads() << " GPU threads";

            std::atomic<bool> stop{false};
            std::thread waiter([&] {
                int sig = 0;
                sigwait(&shutdown, &sig);
                BOOST_LOG_TRIVIAL(debug) << "interrupt signal (" << sig << ") received";
                stop = true;
            });
            waiter_guard guard{waiter, stop};

            // Each dispatch draws a fresh random stream; keep two queued so the GPU never idles
            // between them. The CPU does not touch the histogram until they have completed.
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

            const buddha_kernel::totals t = gpu.sum();
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
