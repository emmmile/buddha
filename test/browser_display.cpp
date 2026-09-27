#include "browser_display.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using buddha_browser::apply_display;
    using buddha_browser::display_settings;
    using buddha_browser::smooth_blur;
    const display_settings preset;
    if (preset.brightness != 25 || preset.contrast != 25 || preset.saturation != 50 ||
        preset.clarity != 25 || preset.texture != 0) {
        std::cerr << "display preset changed unexpectedly\n";
        return 1;
    }

    display_settings display;
    display.brightness = display.contrast = display.saturation = display.clarity = display.texture =
        0;
    const std::vector<uint8_t> original{80, 40, 20, 255};
    auto pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels != original) {
        std::cerr << "neutral display changed the TIFF-style preview\n";
        return 1;
    }

    display.colors[0] = 0x0000ff;
    display.colors[2] = 0xff0000;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != 20 || pixels[1] != 40 || pixels[2] != 80) {
        std::cerr << "channel colors did not remap the source channels\n";
        return 1;
    }

    display.colors = {0xff0000, 0x00ff00, 0x0000ff};
    display.saturation = -100;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != pixels[1] || pixels[1] != pixels[2]) {
        std::cerr << "zero saturation did not produce gray\n";
        return 1;
    }

    display.saturation = 0;
    display.brightness = 25;
    pixels = {0, 230, 255, 255};
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != 0 || pixels[1] <= 230 || pixels[1] >= 255 || pixels[2] != 255) {
        std::cerr << "brightness did not lift midtones and protect endpoints\n";
        return 1;
    }

    display.brightness = 0;
    display.contrast = -100;
    pixels = original;
    apply_display(pixels, 1, 1, display);
    if (pixels[0] != 128 || pixels[1] != 128 || pixels[2] != 128 || pixels[3] != 255) {
        std::cerr << "zero contrast did not converge on the midpoint\n";
        return 1;
    }

    display.contrast = 0;
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

    display.clarity = 100;
    pixels.assign(41 * 41 * 4, 0);
    const size_t bright = (20 * 41 + 20) * 4;
    pixels[bright] = pixels[bright + 1] = pixels[bright + 2] = 255;
    apply_display(pixels, 41, 41, display);
    if (pixels[bright] != 255 || pixels[((20 + 6) * 41 + 20) * 4] != 0 ||
        pixels[((20 + 6) * 41 + 20 + 6) * 4] != 0) {
        std::cerr << "clarity introduced a halo around a clipped bright spot\n";
        return 1;
    }

    std::vector<float> impulse(41 * 41, 0);
    impulse[20 * 41 + 20] = 1;
    const auto broad = smooth_blur(impulse, 41, 41, 4, 3);
    if (broad[(20 + 6) * 41 + 20] <= broad[(20 + 6) * 41 + 20 + 6]) {
        std::cerr << "broad blur still has square support\n";
        return 1;
    }

    display.clarity = 0;
    display.texture = 100;
    pixels.assign(21 * 21 * 4, 0);
    pixels[center] = pixels[center + 1] = pixels[center + 2] = 128;
    apply_display(pixels, 21, 21, display);
    if (pixels[center] <= 128) {
        std::cerr << "texture did not boost fine detail\n";
        return 1;
    }
    return 0;
}
