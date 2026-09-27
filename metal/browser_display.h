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
    std::array<uint32_t, 3> colors{0xff0000, 0x00ff00, 0x0000ff};
    int brightness = 0;   // -100..100, mapped to -2..2 exposure stops
    int contrast = 100;   // 0..200, slope about the midpoint
    int saturation = 100; // 0..200, factor about Rec. 709 luminance
    int clarity = 0;      // -100..100, local contrast at an 8-pixel radius

    bool is_default() const {
        return colors == std::array<uint32_t, 3>{0xff0000, 0x00ff00, 0x0000ff} && brightness == 0 &&
               contrast == 100 && saturation == 100 && clarity == 0;
    }
};

inline float byte_to_unit(uint8_t value) { return float(value) / 255.0f; }
inline uint8_t unit_to_byte(float value) {
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline float luminance(float red, float green, float blue) {
    return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

// The input is the TIFF-style, 8-bit RGB preview. Display controls never touch the histogram.
inline void apply_display(std::vector<uint8_t> &rgba, size_t width, size_t height,
                          const display_settings &display) {
    if (display.is_default())
        return;

    std::array<std::array<float, 3>, 3> color{};
    for (size_t channel = 0; channel < 3; ++channel)
        for (size_t component = 0; component < 3; ++component)
            color[channel][component] =
                float((display.colors[channel] >> (16 - component * 8)) & 0xff) / 255.0f;

    const float exposure = std::exp2(float(display.brightness) / 50.0f);
    const float contrast = float(display.contrast) / 100.0f;
    const float saturation = float(display.saturation) / 100.0f;
    const size_t pixels = width * height;
    for (size_t i = 0; i < pixels; ++i) {
        const size_t offset = i * 4;
        const std::array<float, 3> source{byte_to_unit(rgba[offset]),
                                          byte_to_unit(rgba[offset + 1]),
                                          byte_to_unit(rgba[offset + 2])};
        std::array<float, 3> mixed{};
        for (size_t component = 0; component < 3; ++component) {
            for (size_t channel = 0; channel < 3; ++channel)
                mixed[component] += source[channel] * color[channel][component];
            mixed[component] = (mixed[component] * exposure - 0.5f) * contrast + 0.5f;
        }
        const float gray = luminance(mixed[0], mixed[1], mixed[2]);
        for (size_t component = 0; component < 3; ++component)
            rgba[offset + component] = unit_to_byte(gray + (mixed[component] - gray) * saturation);
    }

    if (display.clarity == 0)
        return;

    // Separable 17x17 box blur of luminance. A positive strength boosts local contrast;
    // a negative strength softens it. The radius is eight output pixels at every zoom.
    constexpr size_t radius = 8;
    std::vector<float> horizontal(pixels);
    for (size_t y = 0; y < height; ++y) {
        float sum = 0;
        for (size_t x = 0; x < std::min(width, radius + 1); ++x) {
            const size_t offset = (y * width + x) * 4;
            sum += luminance(byte_to_unit(rgba[offset]), byte_to_unit(rgba[offset + 1]),
                             byte_to_unit(rgba[offset + 2]));
        }
        for (size_t x = 0; x < width; ++x) {
            if (x > 0) {
                if (x + radius < width) {
                    const size_t offset = (y * width + x + radius) * 4;
                    sum += luminance(byte_to_unit(rgba[offset]), byte_to_unit(rgba[offset + 1]),
                                     byte_to_unit(rgba[offset + 2]));
                }
                if (x > radius) {
                    const size_t offset = (y * width + x - radius - 1) * 4;
                    sum -= luminance(byte_to_unit(rgba[offset]), byte_to_unit(rgba[offset + 1]),
                                     byte_to_unit(rgba[offset + 2]));
                }
            }
            const size_t left = x > radius ? x - radius : 0;
            const size_t right = std::min(width - 1, x + radius);
            horizontal[y * width + x] = sum / float(right - left + 1);
        }
    }

    const float strength = float(display.clarity) / 100.0f;
    for (size_t x = 0; x < width; ++x) {
        float sum = 0;
        for (size_t y = 0; y < std::min(height, radius + 1); ++y)
            sum += horizontal[y * width + x];
        for (size_t y = 0; y < height; ++y) {
            if (y > 0) {
                if (y + radius < height)
                    sum += horizontal[(y + radius) * width + x];
                if (y > radius)
                    sum -= horizontal[(y - radius - 1) * width + x];
            }
            const size_t top = y > radius ? y - radius : 0;
            const size_t bottom = std::min(height - 1, y + radius);
            const float blurred = sum / float(bottom - top + 1);
            const size_t offset = (y * width + x) * 4;
            const float red = byte_to_unit(rgba[offset]);
            const float green = byte_to_unit(rgba[offset + 1]);
            const float blue = byte_to_unit(rgba[offset + 2]);
            const float detail = strength * (luminance(red, green, blue) - blurred);
            rgba[offset] = unit_to_byte(red + detail);
            rgba[offset + 1] = unit_to_byte(green + detail);
            rgba[offset + 2] = unit_to_byte(blue + detail);
        }
    }
}

} // namespace buddha_browser

#endif
