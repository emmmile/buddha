// The float image pipeline behind TIFF output.

#include "buddha.h"
#include "image_pipeline.h"
#include "saver.h"

#include <tiffio.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using buddha_image::adjustments;
using buddha_image::float_image;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

adjustments neutral() {
    adjustments a;
    a.brightness = a.contrast = a.saturation = a.clarity = a.texture = 0;
    return a;
}

float_image solid(size_t width, size_t height, float value) {
    float_image image;
    image.resize(width, height);
    std::fill(image.rgb.begin(), image.rgb.end(), value);
    return image;
}

float_image run(const float_image &base, const adjustments &a) {
    float_image out;
    buddha_image::adjustment_buffers buffers;
    buddha_image::adjust(base, a, out, buffers);
    return out;
}

float at(const float_image &image, size_t x, size_t y, size_t channel = 0) {
    return image.rgb[(y * image.width + x) * 3 + channel];
}

void adjustment_tests() {
    const adjustments preset;
    require(preset.brightness == 25 && preset.contrast == 10 && preset.saturation == 50 &&
                preset.clarity == 50 && preset.texture == 50,
            "display preset changed unexpectedly");

    float_image pixel;
    pixel.resize(1, 1);
    pixel.rgb = {0.3f, 0.15f, 0.08f};
    require(run(pixel, neutral()).rgb == pixel.rgb, "neutral adjustments changed the image");

    auto a = neutral();
    a.saturation = -100;
    auto out = run(pixel, a);
    require(out.rgb[0] == out.rgb[1] && out.rgb[1] == out.rgb[2],
            "zero saturation did not produce gray");

    a = neutral();
    a.brightness = 25;
    pixel.rgb = {0.0f, 0.9f, 1.0f};
    out = run(pixel, a);
    require(out.rgb[0] == 0 && out.rgb[1] > 0.9f && out.rgb[1] < 1 && out.rgb[2] == 1,
            "brightness did not lift midtones and protect endpoints");

    a = neutral();
    a.contrast = -100;
    pixel.rgb = {0.3f, 0.15f, 0.08f};
    out = run(pixel, a);
    std::vector<uint8_t> rgba;
    buddha_image::to_rgba8(out, rgba);
    require(rgba == std::vector<uint8_t>{128, 128, 128, 255},
            "zero contrast did not converge on the midpoint");

    a = neutral();
    a.clarity = 100;
    auto spot = solid(21, 21, 0);
    for (size_t channel = 0; channel < 3; ++channel)
        spot.rgb[(10 * 21 + 10) * 3 + channel] = 0.5f;
    out = run(spot, a);
    require(at(out, 10, 10) > 0.5f && at(out, 10, 10, 1) == at(out, 10, 10) &&
                at(out, 10, 10, 2) == at(out, 10, 10),
            "positive clarity did not boost local contrast");
    a.clarity = -100;
    require(at(run(spot, a), 10, 10) < 0.5f, "negative clarity did not soften local contrast");

    a.clarity = 100;
    auto bright = solid(41, 41, 0);
    for (size_t channel = 0; channel < 3; ++channel)
        bright.rgb[(20 * 41 + 20) * 3 + channel] = 1.0f;
    out = run(bright, a);
    require(at(out, 20, 20) == 1 && at(out, 20, 26) == 0 && at(out, 26, 26) == 0,
            "clarity introduced a halo around a clipped bright spot");

    a = neutral();
    a.texture = 100;
    require(at(run(spot, a), 10, 10) > 0.5f, "texture did not boost fine detail");
}

// The blur before it was row-major and parallel: one serial pass per direction.
std::vector<float> reference_blur(std::vector<float> current, size_t width, size_t height,
                                  size_t radius, int passes) {
    std::vector<float> scratch(current.size());
    for (int pass = 0; pass < passes; ++pass) {
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x) {
                const size_t left = x > radius ? x - radius : 0;
                const size_t right = std::min(width - 1, x + radius);
                float sum = 0;
                for (size_t i = left; i <= right; ++i)
                    sum += current[y * width + i];
                scratch[y * width + x] = sum / float(right - left + 1);
            }
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x) {
                const size_t top = y > radius ? y - radius : 0;
                const size_t bottom = std::min(height - 1, y + radius);
                float sum = 0;
                for (size_t i = top; i <= bottom; ++i)
                    sum += scratch[i * width + x];
                current[y * width + x] = sum / float(bottom - top + 1);
            }
    }
    return current;
}

void blur_tests() {
    std::vector<float> scratch;
    std::vector<float> impulse(41 * 41, 0);
    impulse[20 * 41 + 20] = 1;
    buddha_image::smooth_blur(impulse, 41, 41, 4, 3, scratch);
    require(impulse[(20 + 6) * 41 + 20] > impulse[(20 + 6) * 41 + 20 + 6],
            "broad blur still has square support");

    // Wider than one column strip and taller than one row chunk, so the parallel split shows.
    const size_t width = 613, height = 97;
    std::mt19937 engine(7);
    std::uniform_real_distribution<float> uniform(0, 1);
    std::vector<float> noise(width * height);
    for (auto &value : noise)
        value = uniform(engine);
    const auto expected = reference_blur(noise, width, height, 4, 3);
    buddha_image::smooth_blur(noise, width, height, 4, 3, scratch);
    for (size_t i = 0; i < noise.size(); ++i)
        require(std::abs(noise[i] - expected[i]) < 1e-5f, "parallel blur differs from reference");
}

// A dark gradient finer than 8 bits keeps its steps through a brightness lift; quantizing the
// base to 8 bits first, as the previous preview did, merged them.
void precision_test() {
    float_image ramp;
    ramp.resize(1024, 1);
    for (size_t x = 0; x < ramp.width; ++x)
        for (size_t channel = 0; channel < 3; ++channel)
            ramp.rgb[x * 3 + channel] = 0.1f * float(x) / float(ramp.width);
    auto a = neutral();
    a.brightness = 100;
    std::vector<uint8_t> rgba;
    buddha_image::to_rgba8(run(ramp, a), rgba);
    std::set<uint8_t> levels;
    for (size_t x = 0; x < ramp.width; ++x)
        levels.insert(rgba[x * 4]);
    const size_t base_levels = size_t(0.1f * 256) + 1;
    require(levels.size() > 2 * base_levels, "adjusted gradient shows 8-bit base quantization");
}

std::vector<uint16_t> read_tiff(const std::string &path, uint32_t &width, uint32_t &height) {
    TIFF *tiff = TIFFOpen(path.c_str(), "r");
    require(tiff, "cannot open " + path);
    uint16_t bits = 0, samples = 0;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    require(bits == 16 && samples == 3, "unexpected TIFF sample layout");
    std::vector<uint16_t> pixels(size_t(width) * height * 3);
    for (uint32_t y = 0; y < height; ++y)
        require(TIFFReadScanline(tiff, &pixels[size_t(y) * width * 3], y, 0) >= 0,
                "cannot read TIFF row");
    TIFFClose(tiff);
    return pixels;
}

// Settings for an output image of the given size, which shows the histogram rotated 90 degrees
// clockwise (buddha_tone::output_index).
settings output_settings(uint32_t output_width, uint32_t output_height, double cim) {
    settings s{};
    s.w = output_height;
    s.h = output_width;
    s.cre = -0.5;
    s.cim = cim;
    s.scale = 2.0;
    s.lowr = s.lowg = s.lowb = 0;
    s.highr = s.highg = s.highb = 100;
    s.contrast = 100;
    s.lightness = 100;
    s.threads = 1;
    s.sampler = "naive";
    s.exclusion = "";
    s.no_image = true;
    s.indirect_settings();
    return s;
}

// A preview and a TIFF of the same histogram: same size and orientation, and the preview is the
// TIFF's high byte at neutral display settings.
void orientation_test(uint32_t output_width, uint32_t output_height, double cim) {
    const std::string name = std::to_string(output_width) + "x" + std::to_string(output_height) +
                             (cim == 0 ? " symmetric" : " off-axis");
    settings s = output_settings(output_width, output_height, cim);
    require(s.symmetric_image == (cim == 0), name + ": unexpected symmetry");
    buddha b(s);
    for (size_t i = 0; i < b.raw.size(); ++i)
        b.raw[i].store(uint32_t((i * 2654435761u) % 5000));
    // A marker at histogram column 1, row 0: output column width - 1 (and, mirrored, column 0),
    // output row 1.
    for (size_t channel = 0; channel < 3; ++channel)
        b.raw[3 + channel].store(100000);

    std::vector<uint32_t> counts;
    buddha_image::capture(b.raw, counts);
    float_image base;
    buddha_image::tone_mapper(b.s).map(counts, b.s, base);
    std::vector<uint8_t> preview;
    buddha_image::to_rgba8(base, preview);
    std::vector<uint16_t> preview16;
    buddha_image::to_rgb16(base, preview16);

    const auto path = std::filesystem::temp_directory_path() /
                      ("buddha-orientation-" + std::to_string(getpid()) + ".tiff");
    write_tiff(&b, &b.s, path.string());
    uint32_t width = 0, height = 0;
    const auto tiff = read_tiff(path.string(), width, height);
    std::filesystem::remove(path);

    require(base.width == output_width && base.height == output_height,
            name + ": preview size differs from the request");
    require(width == output_width && height == output_height,
            name + ": TIFF size differs from the request");
    for (size_t i = 0; i < size_t(width) * height; ++i)
        for (size_t channel = 0; channel < 3; ++channel) {
            const uint16_t sample = tiff[i * 3 + channel];
            require(preview16[i * 3 + channel] == sample,
                    name + ": 16-bit pipeline output differs from the TIFF");
            require(preview[i * 4 + channel] == sample >> 8,
                    name + ": preview differs from the TIFF high byte");
        }
    uint16_t peak = 0;
    for (size_t i = 0; i < tiff.size(); i += 3)
        peak = std::max(peak, tiff[i]);
    const size_t marker = size_t(1) * width + width - 1;
    require(peak > 0 && tiff[marker * 3] == peak && preview[marker * 4] == peak >> 8,
            name + ": histogram column 1, row 0 is not at the right of output row 1");
    const size_t mirror = size_t(1) * width;
    require((tiff[mirror * 3] == peak) == (cim == 0),
            name + ": mirrored half does not match the symmetry setting");
}

} // namespace

int main() {
    try {
        adjustment_tests();
        blur_tests();
        precision_test();
        orientation_test(5, 7, 0);    // odd output width: odd histogram height, shared middle row
        orientation_test(6, 4, 0);    // even histogram height
        orientation_test(5, 7, 0.25); // off-axis, no mirroring
        orientation_test(8, 3, -0.4);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
