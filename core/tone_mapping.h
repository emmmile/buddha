#ifndef BUDDHA_TONE_MAPPING_H
#define BUDDHA_TONE_MAPPING_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace buddha_tone {

inline float multiplier(uint32_t maximum, double scale, float contrast, float lightness) {
    return maximum ? float(std::log(scale) / std::pow(float(maximum), contrast) * 70.0 * lightness)
                   : 0.0f;
}

// The 16-bit sample before truncation, from pow(count, contrast).
inline float scaled(float power, float multiplier) {
    return std::clamp(power * multiplier * 256.0f, 0.0f, 65535.0f);
}

inline uint16_t channel16(uint32_t count, float multiplier, float contrast) {
    if (count == 0 || multiplier == 0)
        return 0;
    return uint16_t(scaled(std::pow(float(count), contrast), multiplier));
}

inline uint64_t histogram_index(uint64_t raw_x, uint64_t raw_y, uint64_t width, uint64_t height,
                                bool symmetric, uint64_t histogram_height) {
    if (symmetric && raw_y >= histogram_height)
        raw_y = height - raw_y - 1;
    return (raw_y * width + raw_x) * 3;
}

// TIFF output shows the histogram rotated 90 degrees clockwise: the output is height pixels wide
// and width pixels tall, and output (x, y) shows histogram column y, row height - 1 - x.
struct histogram_point {
    uint64_t x, y;
};
inline histogram_point output_source(uint64_t out_x, uint64_t out_y, uint64_t height) {
    return {out_y, height - out_x - 1};
}

} // namespace buddha_tone

#endif
