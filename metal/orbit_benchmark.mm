#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct config {
    uint32_t orbits = 100000000;
    uint32_t iterations = 1024;
    uint32_t seed = 0x4d595df4U;
    uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
};

struct totals {
    uint64_t iterations = 0;
    uint64_t escaped = 0;
};

struct memory_sample {
    uint64_t resident = 0;
    uint64_t footprint = 0;
    uint64_t metal_allocated = 0;
};

struct memory_stats {
    memory_sample before;
    memory_sample peak;
    memory_sample after;
};

memory_sample read_memory(id<MTLDevice> device = nil) {
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        throw std::runtime_error("unable to read process memory usage");
    return {info.resident_size, info.phys_footprint,
            device == nil ? 0 : static_cast<uint64_t>(device.currentAllocatedSize)};
}

class memory_sampler {
public:
    memory_sampler(memory_stats& stats, id<MTLDevice> device = nil)
        : stats_(stats), device_(device) {
        stats_.before = read_memory(device_);
        stats_.peak = stats_.before;
        worker_ = std::thread([this] {
            while (!stop_.load(std::memory_order_relaxed)) {
                sample();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    ~memory_sampler() { finish(); }

    void finish() {
        if (!worker_.joinable()) return;
        stop_.store(true, std::memory_order_relaxed);
        worker_.join();
        stats_.after = read_memory(device_);
        update_peak(stats_.after);
    }

private:
    void sample() {
        // The worker must not throw across a thread boundary. A failed sample
        // leaves the last successful values intact; finish() checks again.
        try { update_peak(read_memory(device_)); } catch (const std::exception&) {}
    }

    void update_peak(const memory_sample& value) {
        stats_.peak.resident = std::max(stats_.peak.resident, value.resident);
        stats_.peak.footprint = std::max(stats_.peak.footprint, value.footprint);
        stats_.peak.metal_allocated = std::max(stats_.peak.metal_allocated, value.metal_allocated);
    }

    memory_stats& stats_;
    id<MTLDevice> device_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

struct parameters {
    uint32_t orbit_count;
    uint32_t max_iterations;
    uint32_t seed;
    uint32_t padding;
};

uint32_t mix(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

float random01(uint32_t value) {
    return static_cast<float>(mix(value) & 0x00ffffffU) * (1.0f / 16777216.0f);
}

totals run_cpu_range(uint32_t first, uint32_t last, const config& cfg) {
    totals result;
    for (uint32_t gid = first; gid < last; ++gid) {
        const float cr = random01(gid * 2U + cfg.seed) * 4.0f - 2.0f;
        const float ci = random01(gid * 2U + cfg.seed + 1U) * 4.0f - 2.0f;
        float zr = cr;
        float zi = ci;
        for (uint32_t iteration = 0; iteration < cfg.iterations; ++iteration) {
            if (zr * zr + zi * zi > 8.0f) {
                ++result.escaped;
                break;
            }
            const float next_real = zr * zr - zi * zi + cr;
            const float next_imaginary = 2.0f * zr * zi + ci;
            zr = next_real;
            zi = next_imaginary;
            ++result.iterations;
        }
    }
    return result;
}

totals run_cpu(const config& cfg, double& seconds, memory_stats& memory) {
    const uint32_t thread_count = std::min(cfg.threads, cfg.orbits);
    std::vector<totals> partial(thread_count);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    memory_sampler sampler(memory);
    const auto begin = std::chrono::steady_clock::now();
    for (uint32_t thread = 0; thread < thread_count; ++thread) {
        const uint32_t first = static_cast<uint64_t>(cfg.orbits) * thread / thread_count;
        const uint32_t last = static_cast<uint64_t>(cfg.orbits) * (thread + 1) / thread_count;
        workers.emplace_back([&, thread, first, last] { partial[thread] = run_cpu_range(first, last, cfg); });
    }
    for (auto& worker : workers)
        worker.join();
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    sampler.finish();

    totals result;
    for (const totals& part : partial) {
        result.iterations += part.iterations;
        result.escaped += part.escaped;
    }
    return result;
}

const char* metal_source = R"metal(
#include <metal_stdlib>
using namespace metal;

struct Parameters {
    uint orbit_count;
    uint max_iterations;
    uint seed;
    uint padding;
};

uint mix(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float random01(uint value) {
    return float(mix(value) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

kernel void orbit_benchmark(
    constant Parameters& params [[buffer(0)]],
    device uint2* group_results [[buffer(1)]],
    uint gid [[thread_position_in_grid]],
    uint local_id [[thread_index_in_threadgroup]],
    uint group_id [[threadgroup_position_in_grid]]) {
    threadgroup atomic_uint group_iterations;
    threadgroup atomic_uint group_escaped;
    if (local_id == 0) {
        atomic_store_explicit(&group_iterations, 0u, memory_order_relaxed);
        atomic_store_explicit(&group_escaped, 0u, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint iterations = 0u;
    uint escaped = 0u;
    if (gid < params.orbit_count) {
        const float cr = random01(gid * 2u + params.seed) * 4.0f - 2.0f;
        const float ci = random01(gid * 2u + params.seed + 1u) * 4.0f - 2.0f;
        float zr = cr;
        float zi = ci;
        for (uint iteration = 0u; iteration < params.max_iterations; ++iteration) {
            if (zr * zr + zi * zi > 8.0f) {
                escaped = 1u;
                break;
            }
            const float next_real = zr * zr - zi * zi + cr;
            const float next_imaginary = 2.0f * zr * zi + ci;
            zr = next_real;
            zi = next_imaginary;
            ++iterations;
        }
    }
    atomic_fetch_add_explicit(&group_iterations, iterations, memory_order_relaxed);
    atomic_fetch_add_explicit(&group_escaped, escaped, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (local_id == 0)
        group_results[group_id] = uint2(atomic_load_explicit(&group_iterations, memory_order_relaxed),
                                        atomic_load_explicit(&group_escaped, memory_order_relaxed));
}
)metal";

uint32_t parse_uint32(const char* value, const char* option) {
    const unsigned long long parsed = std::stoull(value);
    if (parsed == 0 || parsed > UINT32_MAX)
        throw std::runtime_error(std::string(option) + " must be between 1 and 4294967295");
    return static_cast<uint32_t>(parsed);
}

config parse_arguments(int argc, char** argv) {
    config cfg;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout << "usage: metal-orbit-benchmark [--orbits N] [--iterations N] [--seed N] [--threads N]\n";
            std::exit(0);
        }
        if (index + 1 == argc)
            throw std::runtime_error("missing value for " + option);
        const uint32_t value = parse_uint32(argv[++index], option.c_str());
        if (option == "--orbits") cfg.orbits = value;
        else if (option == "--iterations") cfg.iterations = value;
        else if (option == "--seed") cfg.seed = value;
        else if (option == "--threads") cfg.threads = value;
        else throw std::runtime_error("unknown option: " + option);
    }
    return cfg;
}

totals run_gpu(const config& cfg, double& seconds, memory_stats& memory) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil)
        throw std::runtime_error("no Metal device is available");
    memory_sampler sampler(memory, device);

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:metal_source];
    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
    if (library == nil)
        throw std::runtime_error(std::string("unable to compile Metal kernel: ") + error.localizedDescription.UTF8String);
    id<MTLFunction> function = [library newFunctionWithName:@"orbit_benchmark"];
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
    if (pipeline == nil)
        throw std::runtime_error(std::string("unable to create Metal pipeline: ") + error.localizedDescription.UTF8String);

    constexpr uint32_t threads_per_group = 256;
    const uint32_t groups = static_cast<uint32_t>(
        (static_cast<uint64_t>(cfg.orbits) + threads_per_group - 1) / threads_per_group);
    const parameters params = { cfg.orbits, cfg.iterations, cfg.seed, 0 };
    id<MTLBuffer> parameter_buffer = [device newBufferWithBytes:&params
                                                          length:sizeof(params)
                                                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> result_buffer = [device newBufferWithLength:static_cast<NSUInteger>(groups) * sizeof(uint32_t) * 2
                                                       options:MTLResourceStorageModeShared];
    if (parameter_buffer == nil || result_buffer == nil)
        throw std::runtime_error("unable to allocate Metal benchmark buffers");

    id<MTLCommandQueue> queue = [device newCommandQueue];
    const auto encode = [&] {
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:parameter_buffer offset:0 atIndex:0];
        [encoder setBuffer:result_buffer offset:0 atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(threads_per_group, 1, 1)];
        [encoder endEncoding];
        return command;
    };

    id<MTLCommandBuffer> warmup = encode();
    [warmup commit];
    [warmup waitUntilCompleted];
    if (warmup.status == MTLCommandBufferStatusError)
        throw std::runtime_error(std::string("Metal benchmark warm-up failed: ") +
                                 warmup.error.localizedDescription.UTF8String);

    id<MTLCommandBuffer> command = encode();
    const auto wall_begin = std::chrono::steady_clock::now();
    [command commit];
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError)
        throw std::runtime_error(std::string("Metal benchmark failed: ") + command.error.localizedDescription.UTF8String);
    const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_begin).count();
    seconds = command.GPUEndTime > command.GPUStartTime ? command.GPUEndTime - command.GPUStartTime : wall_seconds;
    sampler.finish();

    const uint32_t* values = static_cast<const uint32_t*>(result_buffer.contents);
    totals result;
    for (uint32_t group = 0; group < groups; ++group) {
        result.iterations += values[group * 2];
        result.escaped += values[group * 2 + 1];
    }
    return result;
}

void print_result(const char* label, const totals& result, double seconds) {
    std::cout << std::fixed << std::setprecision(3)
              << label << ": " << result.iterations / 1000000.0 / seconds << " M orbit iterations/s"
              << ", " << result.escaped << " escaped, " << seconds << " s\n";
}

void print_memory(const char* label, const memory_stats& memory, bool metal) {
    const auto mib = [](uint64_t bytes) { return bytes / 1048576.0; };
    std::cout << std::fixed << std::setprecision(2)
              << label << " memory (MiB, before / sampled peak / after): resident "
              << mib(memory.before.resident) << " / " << mib(memory.peak.resident) << " / "
              << mib(memory.after.resident) << ", footprint "
              << mib(memory.before.footprint) << " / " << mib(memory.peak.footprint) << " / "
              << mib(memory.after.footprint);
    if (metal)
        std::cout << ", Metal allocated " << mib(memory.before.metal_allocated) << " / "
                  << mib(memory.peak.metal_allocated) << " / " << mib(memory.after.metal_allocated);
    std::cout << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            const config cfg = parse_arguments(argc, argv);
            std::cout << "naive float orbit benchmark: " << cfg.orbits << " orbits, "
                      << cfg.iterations << " maximum iterations, seed " << cfg.seed
                      << ", " << cfg.threads << " CPU threads\n";
            double cpu_seconds = 0.0;
            memory_stats cpu_memory;
            const totals cpu = run_cpu(cfg, cpu_seconds, cpu_memory);
            print_result("CPU", cpu, cpu_seconds);
            print_memory("CPU", cpu_memory, false);

            double gpu_seconds = 0.0;
            memory_stats gpu_memory;
            const totals gpu = run_gpu(cfg, gpu_seconds, gpu_memory);
            print_result("Metal", gpu, gpu_seconds);
            print_memory("Metal", gpu_memory, true);
            std::cout << "diagnostic totals: CPU " << cpu.iterations << '/' << cpu.escaped
                      << ", Metal " << gpu.iterations << '/' << gpu.escaped << '\n';
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
