#include "browser_display.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using buddha_browser::apply_display;
    using buddha_browser::display_settings;
    const std::vector<uint8_t> original{80, 40, 20, 255};
    auto pixels = original;
    apply_display(pixels, 1, 1, display_settings{});
    if (pixels != original) {
        std::cerr << "default display changed the TIFF-style preview\n";
        return 1;
    }

    display_settings display;
    display.colors[0] = 0x0000ff;
    display.colors[2] = 0xff0000;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != 20 || pixels[1] != 40 || pixels[2] != 80) {
        std::cerr << "channel colors did not remap the source channels\n";
        return 1;
    }

    display = {};
    display.saturation = 0;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != pixels[1] || pixels[1] != pixels[2]) {
        std::cerr << "zero saturation did not produce gray\n";
        return 1;
    }

    display = {};
    display.brightness = 50;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] <= original[0]) {
        std::cerr << "positive brightness did not increase exposure\n";
        return 1;
    }

    display = {};
    display.contrast = 0;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != 128 || pixels[1] != 128 || pixels[2] != 128 || pixels[3] != 255) {
        std::cerr << "zero contrast did not converge on the midpoint\n";
        return 1;
    }

    display = {};
    display.clarity = 100;
    pixels.assign(21 * 21 * 4, 0);
    for (size_t i = 0; i < 21 * 21; ++i)
        pixels[i * 4 + 3] = 255;
    const size_t center = (10 * 21 + 10) * 4;
    pixels[center] = pixels[center + 1] = pixels[center + 2] = 128;
    apply_display(pixels, 21, 21, display);
    if (pixels[center] <= 128 || pixels[center + 1] != pixels[center] ||
        pixels[center + 2] != pixels[center]) {
        std::cerr << "positive clarity did not boost local contrast\n";
        return 1;
    }
    display.clarity = -100;
    pixels.assign(21 * 21 * 4, 0);
    pixels[center] = pixels[center + 1] = pixels[center + 2] = 128;
    apply_display(pixels, 21, 21, display);
    if (pixels[center] >= 128) {
        std::cerr << "negative clarity did not soften local contrast\n";
        return 1;
    }
    return 0;
}
