#ifndef BUDDHA_IMAGE_PIPELINE_H
#define BUDDHA_IMAGE_PIPELINE_H

// Histogram -> float RGB (TIFF tone curve and orientation) -> display adjustments -> 8-bit RGBA
// preview or 16-bit RGB. One float image carries the whole chain, so the output is quantized
// once, at the end. The browser explorer runs its own WGSL port of the adjustments
// (web/src/shaders/display.wgsl).

#include "settings.h"
#include "tone_mapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__APPLE__) && !defined(BUDDHA_IMAGE_SERIAL)
#include <dispatch/dispatch.h>
#endif

namespace buddha_image {

// Calls body(begin, end) on consecutive chunks of [0, count) of at most grain items, in parallel
// where libdispatch is available. BUDDHA_IMAGE_SERIAL keeps one thread, for measurements.
template <class Body> void parallel_for(size_t count, size_t grain, const Body &body) {
    if (count == 0)
        return;
#if defined(__APPLE__) && !defined(BUDDHA_IMAGE_SERIAL)
    const size_t chunks = (count + grain - 1) / grain;
    if (chunks > 1) {
        struct context {
            const Body *body;
            size_t count, grain;
        } c{&body, count, grain};
        dispatch_apply_f(chunks, DISPATCH_APPLY_AUTO, &c, [](void *data, size_t chunk) {
            const auto &c = *static_cast<const context *>(data);
            const size_t begin = chunk * c.grain;
            (*c.body)(begin, std::min(c.count, begin + c.grain));
        });
        return;
    }
#endif
    for (size_t begin = 0; begin < count; begin += grain)
        body(begin, std::min(count, begin + grain));
}

// Copies a histogram so it can be converted while sampling continues. atomic_wrapper::load() is
// relaxed, so writers may still be running: each count is one the writers stored, but counts
// may be from slightly different moments. Synchronize with the writers first (waitUntilCompleted,
// or joined CPU threads) for an exact copy.
template <class Histogram> void capture(const Histogram &raw, std::vector<uint32_t> &counts) {
    counts.resize(raw.size());
    parallel_for(raw.size(), size_t(1) << 18, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            counts[i] = raw[i].load();
    });
}

inline std::array<uint32_t, 3> channel_maxima(const std::vector<uint32_t> &counts) {
    constexpr size_t grain = size_t(3) << 16;
    std::vector<std::array<uint32_t, 3>> partial((counts.size() + grain - 1) / grain);
    parallel_for(counts.size(), grain, [&](size_t begin, size_t end) {
        std::array<uint32_t, 3> maximum{};
        for (size_t i = begin; i < end; i += 3)
            for (size_t channel = 0; channel < 3; ++channel)
                maximum[channel] = std::max(maximum[channel], counts[i + channel]);
        partial[begin / grain] = maximum;
    });
    std::array<uint32_t, 3> maximum{};
    for (const auto &chunk : partial)
        for (size_t channel = 0; channel < 3; ++channel)
            maximum[channel] = std::max(maximum[channel], chunk[channel]);
    return maximum;
}

// Interleaved RGB in output orientation. 1.0 is the TIFF white point: value * 65536 is the
// 16-bit TIFF sample before truncation.
struct float_image {
    size_t width = 0, height = 0;
    std::vector<float> rgb;

    void resize(size_t w, size_t h) {
        width = w;
        height = h;
        rgb.resize(w * h * 3);
    }
    size_t pixels() const { return width * height; }
};

// The tone curve of core/saver.h. Counts below the table size, almost all of them in practice,
// read a table of the same std::pow results instead of calling it again.
class tone_mapper {
  public:
    static constexpr uint32_t table_size = 1U << 16;

    explicit tone_mapper(const settings &s) : contrast_(s.realContrast), powers_(table_size) {
        for (uint32_t count = 0; count < table_size; ++count)
            powers_[count] = std::pow(float(count), contrast_);
    }

    float unit(uint32_t count, float multiplier) const {
        if (count == 0 || multiplier == 0)
            return 0;
        const float power = count < table_size ? powers_[count] : std::pow(float(count), contrast_);
        return buddha_tone::scaled(power, multiplier) / 65536.0f;
    }

    // Maps a captured histogram (3 counts per histogram pixel) to the rotated output image.
    void map(const std::vector<uint32_t> &counts, const settings &s, float_image &out) const {
        const auto maximum = channel_maxima(counts);
        std::array<float, 3> multiplier{};
        for (size_t channel = 0; channel < 3; ++channel)
            multiplier[channel] =
                buddha_tone::multiplier(maximum[channel], s.scale, contrast_, s.realLightness);

        out.resize(s.h, s.w);
        // Output rows are histogram columns. A band of output rows walks histogram rows in
        // order, reading the band's columns contiguously and writing short runs of each row.
        constexpr size_t band = 32;
        parallel_for(out.height, band, [&](size_t y0, size_t y1) {
            for (size_t x = 0; x < out.width; ++x) {
                for (size_t y = y0; y < y1; ++y) {
                    const size_t in = buddha_tone::output_index(x, y, s.w, s.h, s.symmetric_image,
                                                                s.histogram_height);
                    float *pixel = &out.rgb[(y * out.width + x) * 3];
                    for (size_t channel = 0; channel < 3; ++channel)
                        pixel[channel] = unit(counts[in + channel], multiplier[channel]);
                }
            }
        });
    }

  private:
    float contrast_;
    std::vector<float> powers_;
};

struct adjustments {
    int brightness = 25; // -100..100, highlight-preserving midtone lift
    int contrast = 10;   // -100..100, slope 0..2 about the midpoint
    int saturation = 50; // -100..100, factor 0..2 about Rec. 709 luminance
    int clarity = 50;    // -100..100, broad midtone contrast
    int texture = 50;    // -100..100, fine detail

    bool is_neutral() const {
        return brightness == 0 && contrast == 0 && saturation == 0 && clarity == 0 && texture == 0;
    }
    bool operator==(const adjustments &) const = default;
};

inline float luminance(float red, float green, float blue) {
    return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

inline float lift_midtones(float value, float gain) {
    // Endpoints stay fixed: unlike exposure multiplication, bright values bend toward white.
    return gain * value / (1.0f + (gain - 1.0f) * value);
}

// Several small box passes approximate a smooth Gaussian-like kernel at O(pixels) per pass. A
// single large box has visible square support around bright points. Boxes are clipped and
// renormalized at the edges. Rows, then column strips, run in parallel; both passes read memory
// in row order.
inline void smooth_blur(std::vector<float> &image, size_t width, size_t height, size_t radius,
                        int passes, std::vector<float> &scratch) {
    scratch.resize(image.size());
    for (int pass = 0; pass < passes; ++pass) {
        parallel_for(height, 16, [&](size_t y0, size_t y1) {
            for (size_t y = y0; y < y1; ++y) {
                const float *row = &image[y * width];
                float *out = &scratch[y * width];
                float sum = 0;
                for (size_t x = 0; x < std::min(width, radius + 1); ++x)
                    sum += row[x];
                for (size_t x = 0; x < width; ++x) {
                    if (x > 0) {
                        if (x + radius < width)
                            sum += row[x + radius];
                        if (x > radius)
                            sum -= row[x - radius - 1];
                    }
                    const size_t left = x > radius ? x - radius : 0;
                    const size_t right = std::min(width - 1, x + radius);
                    out[x] = sum / float(right - left + 1);
                }
            }
        });
        constexpr size_t strip = 256;
        parallel_for(width, strip, [&](size_t x0, size_t x1) {
            std::array<float, strip> column_sum{};
            const size_t n = x1 - x0;
            for (size_t y = 0; y < std::min(height, radius + 1); ++y)
                for (size_t x = 0; x < n; ++x)
                    column_sum[x] += scratch[y * width + x0 + x];
            for (size_t y = 0; y < height; ++y) {
                if (y > 0) {
                    if (y + radius < height)
                        for (size_t x = 0; x < n; ++x)
                            column_sum[x] += scratch[(y + radius) * width + x0 + x];
                    if (y > radius)
                        for (size_t x = 0; x < n; ++x)
                            column_sum[x] -= scratch[(y - radius - 1) * width + x0 + x];
                }
                const size_t top = y > radius ? y - radius : 0;
                const size_t bottom = std::min(height - 1, y + radius);
                const float divisor = float(bottom - top + 1);
                for (size_t x = 0; x < n; ++x)
                    image[y * width + x0 + x] = column_sum[x] / divisor;
            }
        });
    }
}

// Reused between frames so a slider drag does not reallocate.
struct adjustment_buffers {
    std::vector<float> luma, broad, fine, scratch;
};

// Applies the display adjustments to a tone-mapped image. Output values stay in [0, 1].
inline void adjust(const float_image &base, const adjustments &a, float_image &out,
                   adjustment_buffers &buffers) {
    out.resize(base.width, base.height);
    const size_t width = base.width, height = base.height, pixels = base.pixels();
    constexpr size_t rows = 16;
    if (a.is_neutral()) {
        parallel_for(height, rows, [&](size_t y0, size_t y1) {
            std::copy(base.rgb.begin() + y0 * width * 3, base.rgb.begin() + y1 * width * 3,
                      out.rgb.begin() + y0 * width * 3);
        });
        return;
    }

    const float brightness_gain = std::exp2(float(a.brightness) / 50.0f);
    const float contrast = 1.0f + float(a.contrast) / 100.0f;
    const float saturation = 1.0f + float(a.saturation) / 100.0f;
    const bool detail = a.clarity != 0 || a.texture != 0;
    if (detail)
        buffers.luma.resize(pixels);
    parallel_for(height, rows, [&](size_t y0, size_t y1) {
        for (size_t i = y0 * width; i < y1 * width; ++i) {
            std::array<float, 3> channels;
            for (size_t channel = 0; channel < 3; ++channel)
                channels[channel] = (base.rgb[i * 3 + channel] - 0.5f) * contrast + 0.5f;
            const float gray = luminance(channels[0], channels[1], channels[2]);
            float *pixel = &out.rgb[i * 3];
            for (size_t channel = 0; channel < 3; ++channel)
                pixel[channel] = lift_midtones(
                    std::clamp(gray + (channels[channel] - gray) * saturation, 0.0f, 1.0f),
                    brightness_gain);
            if (detail)
                buffers.luma[i] = luminance(pixel[0], pixel[1], pixel[2]);
        }
    });
    if (!detail)
        return;

    if (a.clarity) {
        buffers.broad = buffers.luma;
        smooth_blur(buffers.broad, width, height, 4, 3, buffers.scratch);
    }
    if (a.texture) {
        buffers.fine = buffers.luma;
        smooth_blur(buffers.fine, width, height, 2, 2, buffers.scratch);
    }
    const float clarity = float(a.clarity) / 100.0f;
    const float texture = float(a.texture) / 100.0f;
    parallel_for(height, rows, [&](size_t y0, size_t y1) {
        for (size_t i = y0 * width; i < y1 * width; ++i) {
            const float luma = buffers.luma[i];
            const float midtone = 4.0f * luma * (1.0f - luma);
            float delta = 0;
            if (a.clarity)
                delta += clarity * (luma - buffers.broad[i]) * midtone * midtone;
            if (a.texture)
                delta += texture * (luma - buffers.fine[i]) * midtone;
            for (size_t channel = 0; channel < 3; ++channel)
                out.rgb[i * 3 + channel] = std::clamp(out.rgb[i * 3 + channel] + delta, 0.0f, 1.0f);
        }
    });
}

// Truncating quantizers, like the TIFF writer: a neutral image reproduces its samples exactly.
inline uint8_t to_u8(float value) {
    return uint8_t(std::min(std::clamp(value, 0.0f, 1.0f) * 256.0f, 255.0f));
}
inline uint16_t to_u16(float value) {
    return uint16_t(std::min(std::clamp(value, 0.0f, 1.0f) * 65536.0f, 65535.0f));
}

inline void to_rgba8(const float_image &image, std::vector<uint8_t> &rgba) {
    rgba.resize(image.pixels() * 4);
    parallel_for(image.height, 16, [&](size_t y0, size_t y1) {
        for (size_t i = y0 * image.width; i < y1 * image.width; ++i) {
            for (size_t channel = 0; channel < 3; ++channel)
                rgba[i * 4 + channel] = to_u8(image.rgb[i * 3 + channel]);
            rgba[i * 4 + 3] = 255;
        }
    });
}

inline void to_rgb16(const float_image &image, std::vector<uint16_t> &rgb) {
    rgb.resize(image.rgb.size());
    parallel_for(image.height, 16, [&](size_t y0, size_t y1) {
        for (size_t i = y0 * image.width * 3; i < y1 * image.width * 3; ++i)
            rgb[i] = to_u16(image.rgb[i]);
    });
}

} // namespace buddha_image

#endif
