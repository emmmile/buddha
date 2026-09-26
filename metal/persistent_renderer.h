#ifndef BUDDHA_METAL_PERSISTENT_RENDERER_H
#define BUDDHA_METAL_PERSISTENT_RENDERER_H

// Objective-C++ (ARC) host side of metal/render.metal.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "buddha_kernel.h"
#include "buddha_metal_source.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace buddha_metal {

using buddha_kernel::parameters;
using buddha_kernel::totals;

constexpr uint32_t group_size = 256;

inline std::string describe(NSError *error) {
    return error ? error.localizedDescription.UTF8String : "unknown error";
}

// nil when the system has no Metal device (for example some virtual machines).
inline id<MTLDevice> find_device() { return MTLCreateSystemDefaultDevice(); }

inline id<MTLDevice> default_device() {
    id<MTLDevice> device = find_device();
    if (device == nil)
        throw std::runtime_error("no Metal device is available");
    return device;
}

// Safe math keeps IEEE float semantics (no fast-math reassociation), like the CPU reference in
// test/metal_consistency.mm.
inline id<MTLComputePipelineState> compile(id<MTLDevice> device) {
    MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
    if (@available(macOS 15.0, *))
        options.mathMode = MTLMathModeSafe;
    NSError *error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithSource:[NSString stringWithUTF8String:buddha_metal_source]
                             options:options
                               error:&error];
    if (library == nil)
        throw std::runtime_error("Metal compile failed: " + describe(error));
    id<MTLFunction> function = [library newFunctionWithName:@"render"];
    id<MTLComputePipelineState> state = [device newComputePipelineStateWithFunction:function
                                                                              error:&error];
    if (state == nil)
        throw std::runtime_error("Metal pipeline failed: " + describe(error));
    return state;
}

// Runs the render kernel on an exclusion map and a histogram that live in CPU memory. The
// histogram is wrapped, not copied: on Apple silicon the GPU writes the caller's memory directly,
// so it must be page-aligned, span whole pages, and outlive the renderer (buddha::vector_type
// uses page_allocator for this). Its atomics are 32-bit, the same layout as
// atomic_wrapper<uint32_t>.
class persistent_renderer {
  public:
    // Thread count and samples per dispatch measured best on an M5 Pro: flat between about 24k
    // and 64k threads; large dispatches shrink the tail where lanes wait for the last long
    // orbits.
    static constexpr uint32_t default_threads = 32768;
    static constexpr uint32_t default_batch = 1U << 28;

    persistent_renderer(id<MTLDevice> device, const parameters &base, const uint8_t *map,
                        size_t map_bytes, void *histogram, size_t histogram_bytes,
                        uint32_t threads = default_threads)
        : device_(device), base_(base),
          threads_((threads + group_size - 1) / group_size * group_size) {
        const size_t page = size_t(getpagesize());
        if (reinterpret_cast<uintptr_t>(histogram) % page != 0 || histogram_bytes % page != 0)
            throw std::invalid_argument("the histogram must be page-aligned whole pages");
        if (histogram_bytes > device_.maxBufferLength)
            throw std::runtime_error("histogram exceeds the Metal device's maximum buffer "
                                     "length");
        pipeline_ = compile(device_);
        queue_ = [device_ newCommandQueue];
        map_ = [device_ newBufferWithBytes:map
                                    length:map_bytes
                                   options:MTLResourceStorageModeShared];
        raw_ = [device_ newBufferWithBytesNoCopy:histogram
                                          length:histogram_bytes
                                         options:MTLResourceStorageModeShared
                                     deallocator:nil];
        totals_ = [device_ newBufferWithLength:threads_ * sizeof(totals)
                                       options:MTLResourceStorageModeShared];
        if (queue_ == nil || map_ == nil || raw_ == nil || totals_ == nil)
            throw std::runtime_error("unable to create Metal buffers");
        clear_totals();
    }

    uint32_t threads() const { return threads_; }
    id<MTLDevice> device() const { return device_; }

    void clear_totals() { std::memset(totals_.contents, 0, threads_ * sizeof(totals)); }

    // Commits one dispatch over samples [offset, offset + count) of the stream (key0, key1).
    // Dispatches on the queue run in order, which the per-thread counters rely on.
    id<MTLCommandBuffer> dispatch(uint32_t offset, uint32_t count, uint32_t key0, uint32_t key1) {
        parameters p = base_;
        p.offset = offset;
        p.count = count;
        p.key0 = key0;
        p.key1 = key1;
        p.threads = threads_;
        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline_];
        [encoder setBytes:&p length:sizeof(p) atIndex:0];
        [encoder setBuffer:map_ offset:0 atIndex:1];
        [encoder setBuffer:raw_ offset:0 atIndex:2];
        [encoder setBuffer:totals_ offset:0 atIndex:3];
        [encoder dispatchThreads:MTLSizeMake(threads_, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
        [encoder endEncoding];
        [command commit];
        return command;
    }

    static void wait(id<MTLCommandBuffer> command) {
        [command waitUntilCompleted];
        if (command.status == MTLCommandBufferStatusError)
            throw std::runtime_error("Metal command failed: " + describe(command.error));
    }

    totals sum() const {
        totals s{};
        const auto *t = static_cast<const totals *>(totals_.contents);
        for (uint32_t i = 0; i < threads_; ++i) {
            s.iterations += t[i].iterations;
            s.redraw += t[i].redraw;
            s.escaped += t[i].escaped;
            s.excluded += t[i].excluded;
            s.periodic += t[i].periodic;
            s.increments += t[i].increments;
        }
        return s;
    }

  private:
    id<MTLDevice> device_;
    parameters base_;
    uint32_t threads_;
    id<MTLComputePipelineState> pipeline_;
    id<MTLCommandQueue> queue_;
    id<MTLBuffer> map_, raw_, totals_;
};

// Kernel parameters for a renderer settings object. The window comes from
// settings::histogram_geometry, the same one the CPU renderer draws with.
template <class Settings> parameters make_parameters(const Settings &s) {
    const buddha_kernel::geometry<double> g = s.histogram_geometry();
    parameters p{};
    p.low = s.low;
    p.high = s.high;
    p.lowr = s.lowr;
    p.highr = s.highr;
    p.lowg = s.lowg;
    p.highg = s.highg;
    p.lowb = s.lowb;
    p.highb = s.highb;
    p.width = g.width;
    p.histogram_height = g.height;
    p.symmetric = g.symmetric;
    p.odd_center = g.odd_center;
    p.exclusion_size = uint32_t(s.exclusion_size);
    p.minre = float(g.minre);
    p.maxim = float(g.maxim);
    p.scale = float(g.scale);
    return p;
}

// Bytes to wrap for a histogram allocated with page_allocator.
template <class Vector> size_t histogram_bytes(const Vector &v) {
    return Vector::allocator_type::allocation_size(v.capacity());
}

// A GPU-visible histogram must have exactly the layout of 32-bit lock-free atomics.
template <class Vector> void check_histogram_layout() {
    typedef typename Vector::value_type value_type;
    static_assert(sizeof(value_type) == sizeof(uint32_t), "histogram element must be 32-bit");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "32-bit atomics must be lock-free");
}

} // namespace buddha_metal

#endif
