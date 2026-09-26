#ifndef BUDDHA_METAL_PERSISTENT_RENDERER_H
#define BUDDHA_METAL_PERSISTENT_RENDERER_H

// Objective-C++ (ARC) host side of the persistent Metal kernel in render_kernel.h.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "render_kernel.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace buddha_metal {

inline std::string describe(NSError *error) {
    return error ? error.localizedDescription.UTF8String : "unknown error";
}

inline id<MTLDevice> default_device() {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil)
        throw std::runtime_error("no Metal device is available");
    return device;
}

// Safe math keeps float semantics (no fast-math reassociation), matching the CPU float mirror.
inline id<MTLLibrary> compile(id<MTLDevice> device, const std::string &source) {
    MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
    if (@available(macOS 15.0, *))
        options.mathMode = MTLMathModeSafe;
    NSError *error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
                             options:options
                               error:&error];
    if (library == nil)
        throw std::runtime_error("Metal compile failed: " + describe(error));
    return library;
}

inline id<MTLComputePipelineState> pipeline(id<MTLDevice> device, id<MTLLibrary> library,
                                            NSString *name) {
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (function == nil)
        throw std::runtime_error(std::string("missing Metal function ") + name.UTF8String);
    NSError *error = nil;
    id<MTLComputePipelineState> state = [device newComputePipelineStateWithFunction:function
                                                                              error:&error];
    if (state == nil)
        throw std::runtime_error("Metal pipeline failed: " + describe(error));
    return state;
}

// Owns the exclusion map, the RGB histogram and per-thread counters on the GPU, and dispatches
// the persistent kernel. Buffers use shared storage, so the CPU reads them directly on Apple
// silicon once the dispatches that wrote them have completed.
class persistent_renderer {
  public:
    // Thread count and samples per dispatch measured best on an M5 Pro: flat between about 24k
    // and 64k threads; large dispatches shrink the tail where lanes wait for the last long
    // orbits.
    static constexpr uint32_t default_threads = 32768;
    static constexpr uint32_t default_batch = 1U << 28;

    persistent_renderer(id<MTLDevice> device, const parameters &base, const uint8_t *map,
                        size_t map_bytes, uint64_t bins, uint32_t threads = default_threads)
        : device_(device), base_(base),
          threads_((threads + group_size - 1) / group_size * group_size), bins_(bins) {
        library_ = compile(device_, kernel_source);
        pipeline_ = pipeline(device_, library_, @"render_persistent");
        queue_ = [device_ newCommandQueue];
        map_ = [device_ newBufferWithBytes:map
                                    length:map_bytes
                                   options:MTLResourceStorageModeShared];
        raw_ = [device_ newBufferWithLength:bins_ * sizeof(uint32_t)
                                    options:MTLResourceStorageModeShared];
        totals_ = [device_ newBufferWithLength:threads_ * sizeof(thread_totals)
                                       options:MTLResourceStorageModeShared];
        if (queue_ == nil || map_ == nil || raw_ == nil || totals_ == nil)
            throw std::runtime_error("unable to allocate Metal buffers (" +
                                     std::to_string(bins_ * sizeof(uint32_t) >> 20) +
                                     " MiB histogram)");
        clear();
    }

    uint32_t *histogram() { return static_cast<uint32_t *>(raw_.contents); }
    uint64_t bins() const { return bins_; }
    uint32_t threads() const { return threads_; }
    id<MTLDevice> device() const { return device_; }

    void clear() {
        std::memset(raw_.contents, 0, bins_ * sizeof(uint32_t));
        std::memset(totals_.contents, 0, threads_ * sizeof(thread_totals));
    }

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

    thread_totals totals() const {
        thread_totals sum{};
        const auto *t = static_cast<const thread_totals *>(totals_.contents);
        for (uint32_t i = 0; i < threads_; ++i) {
            sum.iterations += t[i].iterations;
            sum.redraw += t[i].redraw;
            sum.escaped += t[i].escaped;
            sum.excluded += t[i].excluded;
            sum.periodic += t[i].periodic;
            sum.increments += t[i].increments;
        }
        return sum;
    }

  private:
    id<MTLDevice> device_;
    parameters base_;
    uint32_t threads_;
    uint64_t bins_;
    id<MTLLibrary> library_;
    id<MTLComputePipelineState> pipeline_;
    id<MTLCommandQueue> queue_;
    id<MTLBuffer> map_, raw_, totals_;
};

// Kernel parameters for a renderer settings object (float geometry, renderer colour ranges).
template <class Settings> parameters make_parameters(const Settings &s) {
    parameters p{};
    p.low = s.low;
    p.high = s.high;
    p.lowr = s.lowr;
    p.highr = s.highr;
    p.lowg = s.lowg;
    p.highg = s.highg;
    p.lowb = s.lowb;
    p.highb = s.highb;
    p.width = uint32_t(s.w);
    p.histogram_height = uint32_t(s.histogram_height);
    p.symmetric = s.symmetric_image;
    p.odd_center = s.symmetric_image && s.h % 2 != 0;
    p.exclusion_size = uint32_t(s.exclusion_size);
    p.minre = float(s.minre);
    p.maxim = float(s.maxim);
    p.scale = float(s.scale);
    return p;
}

} // namespace buddha_metal

#endif
