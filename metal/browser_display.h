#ifndef BUDDHA_BROWSER_DISPLAY_H
#define BUDDHA_BROWSER_DISPLAY_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace buddha_browser {

struct display_settings {
    int brightness = 25; // -100..100, highlight-preserving midtone lift
    int contrast = 10;   // -100..100, slope 0..2 about the midpoint
    int saturation = 50; // -100..100, factor 0..2 about Rec. 709 luminance
    int clarity = 50;    // -100..100, broad midtone contrast
    int texture = 50;    // -100..100, fine detail

    bool is_neutral() const {
        return brightness == 0 && contrast == 0 && saturation == 0 && clarity == 0 && texture == 0;
    }
};

inline float byte_to_unit(uint8_t value) { return float(value) / 255.0f; }
inline uint8_t unit_to_byte(float value) {
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline float luminance(float red, float green, float blue) {
    return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

inline float lift_midtones(float value, float gain) {
    // Endpoints stay fixed: unlike exposure multiplication, bright values bend toward white.
    return gain * value / (1.0f + (gain - 1.0f) * value);
}

// Several small box passes approximate a smooth Gaussian-like kernel at O(pixels)
// per pass. A single large box has visible square support around bright points.
inline std::vector<float> smooth_blur(const std::vector<float> &source, size_t width, size_t height,
                                      size_t radius, int passes) {
    std::vector<float> current = source;
    std::vector<float> scratch(source.size());
    for (int pass = 0; pass < passes; ++pass) {
        for (size_t y = 0; y < height; ++y) {
            float sum = 0;
            for (size_t x = 0; x < std::min(width, radius + 1); ++x)
                sum += current[y * width + x];
            for (size_t x = 0; x < width; ++x) {
                if (x > 0) {
                    if (x + radius < width)
                        sum += current[y * width + x + radius];
                    if (x > radius)
                        sum -= current[y * width + x - radius - 1];
                }
                const size_t left = x > radius ? x - radius : 0;
                const size_t right = std::min(width - 1, x + radius);
                scratch[y * width + x] = sum / float(right - left + 1);
            }
        }
        for (size_t x = 0; x < width; ++x) {
            float sum = 0;
            for (size_t y = 0; y < std::min(height, radius + 1); ++y)
                sum += scratch[y * width + x];
            for (size_t y = 0; y < height; ++y) {
                if (y > 0) {
                    if (y + radius < height)
                        sum += scratch[(y + radius) * width + x];
                    if (y > radius)
                        sum -= scratch[(y - radius - 1) * width + x];
                }
                const size_t top = y > radius ? y - radius : 0;
                const size_t bottom = std::min(height - 1, y + radius);
                current[y * width + x] = sum / float(bottom - top + 1);
            }
        }
    }
    return current;
}

// The input is the TIFF-style, 8-bit RGB preview. Display controls never touch the histogram.
inline void apply_display(std::vector<uint8_t> &rgba, size_t width, size_t height,
                          const display_settings &display) {
    if (display.is_neutral())
        return;

    const float brightness_gain = std::exp2(float(display.brightness) / 50.0f);
    const float contrast = 1.0f + float(display.contrast) / 100.0f;
    const float saturation = 1.0f + float(display.saturation) / 100.0f;
    const size_t pixels = width * height;
    for (size_t i = 0; i < pixels; ++i) {
        const size_t offset = i * 4;
        std::array<float, 3> channels{byte_to_unit(rgba[offset]), byte_to_unit(rgba[offset + 1]),
                                      byte_to_unit(rgba[offset + 2])};
        for (auto &channel : channels)
            channel = (channel - 0.5f) * contrast + 0.5f;
        const float gray = luminance(channels[0], channels[1], channels[2]);
        for (size_t component = 0; component < 3; ++component)
            rgba[offset + component] = unit_to_byte(lift_midtones(
                std::clamp(gray + (channels[component] - gray) * saturation, 0.0f, 1.0f),
                brightness_gain));
    }

    if (display.clarity == 0 && display.texture == 0)
        return;

    std::vector<float> luma(pixels);
    for (size_t i = 0; i < pixels; ++i) {
        const size_t offset = i * 4;
        luma[i] = luminance(byte_to_unit(rgba[offset]), byte_to_unit(rgba[offset + 1]),
                            byte_to_unit(rgba[offset + 2]));
    }
    const auto broad =
        display.clarity ? smooth_blur(luma, width, height, 4, 3) : std::vector<float>{};
    const auto fine =
        display.texture ? smooth_blur(luma, width, height, 2, 2) : std::vector<float>{};
    for (size_t i = 0; i < pixels; ++i) {
        const size_t offset = i * 4;
        const float midtone = 4.0f * luma[i] * (1.0f - luma[i]);
        float detail = 0;
        if (display.clarity)
            detail += float(display.clarity) / 100.0f * (luma[i] - broad[i]) * midtone * midtone;
        if (display.texture)
            detail += float(display.texture) / 100.0f * (luma[i] - fine[i]) * midtone;
        for (size_t component = 0; component < 3; ++component)
            rgba[offset + component] =
                unit_to_byte(byte_to_unit(rgba[offset + component]) + detail);
    }
}

} // namespace buddha_browser

#endif
