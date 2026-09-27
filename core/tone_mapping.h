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

inline uint16_t channel16(uint32_t count, float multiplier, float contrast) {
    if (count == 0 || multiplier == 0)
        return 0;
    const float value = std::pow(float(count), contrast) * multiplier * 256.0f;
    return uint16_t(std::clamp(value, 0.0f, 65535.0f));
}

inline uint8_t channel8(uint32_t count, float multiplier, float contrast) {
    return uint8_t(channel16(count, multiplier, contrast) >> 8);
}

inline uint64_t histogram_index(uint64_t raw_x, uint64_t raw_y, uint64_t width, uint64_t height,
                                bool symmetric, uint64_t histogram_height) {
    if (symmetric && raw_y >= histogram_height)
        raw_y = height - raw_y - 1;
    return (raw_y * width + raw_x) * 3;
}

} // namespace buddha_tone

#endif
