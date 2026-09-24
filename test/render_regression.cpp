#include "buddha.h"
#include "buddha_generator.h"
#include "saver.h"

#include <boost/archive/binary_oarchive.hpp>
#include <zstd.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <tiffio.h>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

settings make_settings() {
    settings s{};
    s.w = 4;
    s.h = 3;
    s.scale = 2.0;
    s.cre = 0.0;
    s.cim = 0.0;
    s.lowr = s.lowg = s.lowb = 0;
    s.highr = s.highg = s.highb = 32;
    s.contrast = s.lightness = 100;
    s.threads = 1;
    s.exclusion_size = 10;
    s.formula = "z = z * z + c";
    s.no_image = true;
    s.indirect_settings();
    return s;
}

void geometry_test() {
    namespace fs = std::filesystem;
    const auto image_path =
        fs::temp_directory_path() /
        ("buddha-image-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tiff");
    auto s = make_settings();
    buddha from_temporary(make_settings());
    buddha::complex_type visible(0.0, 0.0);
    require(from_temporary.core.inside(visible), "core settings must outlive a temporary argument");
    from_temporary.startGenerators();
    from_temporary.stopGenerators();
    buddha image(s);
    buddha_generator generator(image.core, image.raw, image.s, 1);
    require(image.raw.size() == 4 * 2 * 3, "odd-height histogram allocation");

    buddha::complex_type center(0.25, 0.0);
    generator.drawPoint(center, true, false, false);
    require(image.raw[(1 * 4 + 2) * 3].load() == 1, "odd-height center row");

    rgb_view<boost::gil::rgb16_pixel_t> view(&image, &image.s, {4, 3});
    require(boost::gil::at_c<0>(view({2, 1})) > 0, "odd-height image lookup");
    write_tiff(&image, &image.s, image_path.string());
    {
        TIFF *file = TIFFOpen(image_path.c_str(), "r");
        require(file != nullptr, "odd-height TIFF open");
        uint16_t row[3 * 3]{};
        for (uint32_t y = 0; y <= 2; ++y)
            require(TIFFReadScanline(file, row, y) == 1, "odd-height TIFF read");
        require(row[3] > 0, "odd-height TIFF center row");
        TIFFClose(file);
    }

    s.h = 4;
    s.cim = 1.0;
    s.indirect_settings();
    buddha off_axis(s);
    buddha_generator off_axis_generator(off_axis.core, off_axis.raw, off_axis.s, 1);
    require(off_axis.raw.size() == 4 * 4 * 3, "off-axis histogram allocation");
    buddha::complex_type lower(0.25, 0.5);
    buddha::complex_type outside(0.25, -0.5);
    off_axis_generator.drawPoint(lower, true, false, false);
    off_axis_generator.drawPoint(outside, true, false, false);
    require(off_axis.raw[(3 * 4 + 2) * 3].load() == 1, "off-axis lower row");
    rgb_view<boost::gil::rgb16_pixel_t> off_axis_view(&off_axis, &off_axis.s, {4, 4});
    require(boost::gil::at_c<0>(off_axis_view({2, 3})) > 0, "off-axis image lookup");
    require(boost::gil::at_c<0>(off_axis_view({2, 0})) == 0, "off-axis image must not mirror");
    require(!off_axis.core.inside(outside), "off-axis contribution test must not mirror");
    write_tiff(&off_axis, &off_axis.s, image_path.string());
    {
        TIFF *file = TIFFOpen(image_path.c_str(), "r");
        require(file != nullptr, "off-axis TIFF open");
        uint16_t row[3 * 4]{};
        for (uint32_t y = 0; y <= 2; ++y)
            require(TIFFReadScanline(file, row, y) == 1, "off-axis TIFF read");
        require(row[0] > 0 && row[9] == 0, "off-axis TIFF row must not mirror");
        TIFFClose(file);
    }
    fs::remove(image_path);
}

void exclusion_test() {
    namespace fs = std::filesystem;
    auto s = make_settings();
    s.threads = 3;
    s.exclusion = (fs::temp_directory_path() /
                   ("buddha-exclusion-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
                      .string();
    mandelbrot<buddha::complex_type> map(s);
    map.data.assign(map.data.size(), 2);
    map.exclusion();
    for (auto cell : map.data)
        require(cell <= 1, "exclusion-map worker left a cell uncomputed");
    map.save();
    mandelbrot<buddha::complex_type> loaded(s);
    require(loaded.load(), "exclusion-map load");
    require(loaded.data == map.data, "exclusion-map round trip");
    fs::remove(s.exclusion);
}

void checkpoint_test() {
    namespace fs = std::filesystem;
    auto s = make_settings();
    const auto path = fs::temp_directory_path() /
                      ("buddha-regression-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    s.outfile = path.string();
    {
        buddha image(s);
        image.totaltime = 1.0;
        image.raw[0].store(7);
        image.save();
    }

    s.infile = path.string() + ".zst";
    {
        buddha loaded(s);
        require(loaded.raw[0].load() == 7, "checkpoint round trip");
    }

    s.highr = 64;
    s.indirect_settings();
    bool rejected = false;
    try {
        buddha mismatch(s);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "mismatched checkpoint settings were accepted");

    s = make_settings();
    s.h = 4;
    s.indirect_settings();
    s.infile = path.string() + ".zst";
    s.outfile = path.string();
    std::ostringstream archive_bytes(std::ios::binary);
    {
        boost::archive::binary_oarchive archive(archive_bytes);
        std::vector<buddha::pixel> old_values(3 * s.size, 0);
        old_values[0] = 9;
        archive.save_binary(old_values.data(), old_values.size() * sizeof(old_values[0]));
    }
    const auto legacy_bytes = archive_bytes.str();
    std::vector<char> compressed(ZSTD_compressBound(legacy_bytes.size()));
    const auto written = ZSTD_compress(compressed.data(), compressed.size(), legacy_bytes.data(),
                                       legacy_bytes.size(), 1);
    require(!ZSTD_isError(written), "legacy fixture compression");
    {
        std::ofstream output(s.infile, std::ios::binary | std::ios::trunc);
        output.write(compressed.data(), written);
    }
    rejected = false;
    try {
        buddha legacy(s);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "legacy checkpoint loaded without opt-in");
    s.allow_legacy_checkpoint = true;
    {
        buddha legacy(s);
        require(legacy.raw[0].load() == 9, "legacy checkpoint opt-in");
    }
    fs::remove(s.infile);
}
} // namespace

int main() {
    geometry_test();
    exclusion_test();
    checkpoint_test();
}
