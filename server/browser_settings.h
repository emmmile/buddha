#ifndef BUDDHA_BROWSER_SETTINGS_H
#define BUDDHA_BROWSER_SETTINGS_H

// Request parsing and render settings for the browser prototype, kept free of Objective-C and
// HTTP so tests can build the same settings.

#include "image_pipeline.h"
#include "settings.h"

#include <boost/property_tree/json_parser.hpp>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace buddha_browser {

constexpr uint64_t max_pixels = 2'000'000;

struct request_settings {
    uint32_t output_width, output_height;
    double cre, cim, scale;
    uint32_t lowr, lowg, lowb, highr, highg, highb;
};

inline request_settings parse_settings(const std::string &body) {
    std::istringstream input(body);
    boost::property_tree::ptree json;
    boost::property_tree::read_json(input, json);
    request_settings c{
        json.get<uint32_t>("width"), json.get<uint32_t>("height"), json.get<double>("cre"),
        json.get<double>("cim"),     json.get<double>("scale"),    json.get<uint32_t>("lowr"),
        json.get<uint32_t>("lowg"),  json.get<uint32_t>("lowb"),   json.get<uint32_t>("highr"),
        json.get<uint32_t>("highg"), json.get<uint32_t>("highb"),
    };
    if (c.output_width == 0 || c.output_height == 0 ||
        uint64_t(c.output_width) * c.output_height > max_pixels)
        throw std::invalid_argument("viewport exceeds the two-million-pixel prototype limit");
    if (!std::isfinite(c.cre) || !std::isfinite(c.cim) || !std::isfinite(c.scale) || c.scale <= 0)
        throw std::invalid_argument("center and scale must be finite; scale must be positive");
    if (c.highr == 0 || c.highg == 0 || c.highb == 0 || c.highr > 100'000 || c.highg > 100'000 ||
        c.highb > 100'000 || c.lowr >= c.highr || c.lowg >= c.highg || c.lowb >= c.highb)
        throw std::invalid_argument("invalid channel iteration ranges");
    return c;
}

inline buddha_image::adjustments parse_display(const std::string &body) {
    std::istringstream input(body);
    boost::property_tree::ptree json;
    boost::property_tree::read_json(input, json);
    buddha_image::adjustments display;
    display.brightness = json.get<int>("brightness");
    display.contrast = json.get<int>("contrast");
    display.saturation = json.get<int>("saturation");
    display.clarity = json.get<int>("clarity");
    display.texture = json.get<int>("texture");
    if (display.brightness < -100 || display.brightness > 100 || display.contrast < -100 ||
        display.contrast > 100 || display.saturation < -100 || display.saturation > 100 ||
        display.clarity < -100 || display.clarity > 100 || display.texture < -100 ||
        display.texture > 100)
        throw std::invalid_argument("display values are outside their supported ranges");
    return display;
}

inline settings make_settings(const request_settings &c) {
    settings s{};
    // The output shows the histogram rotated 90 degrees clockwise (buddha_tone::output_index).
    s.w = c.output_height;
    s.h = c.output_width;
    s.cre = c.cre;
    s.cim = c.cim;
    s.scale = c.scale;
    s.lowr = c.lowr;
    s.lowg = c.lowg;
    s.lowb = c.lowb;
    s.highr = c.highr;
    s.highg = c.highg;
    s.highb = c.highb;
    s.contrast = 100;
    s.lightness = 100;
    s.threads = 1;
    s.sampler = "naive";
    s.exclusion = BUDDHA_EXCLUSION_MAP;
    s.no_image = true;
    s.indirect_settings();
    return s;
}

} // namespace buddha_browser

#endif
