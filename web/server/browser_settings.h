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

// Metropolis sampler settings. The radius is relative to the view, so a preview matches a larger
// render. The original CPU sampler (buddha_generator::metropolis) used a radius of 20 / 8192 of
// an 8192-pixel render (0.244%) and a length exponent of 2; the defaults here, chosen in the
// browser, give a smoother image with the same bias toward long orbits.
struct metropolis_settings {
    double radius = 1; // percent of the view's shorter side
    double exponent_l = 1, exponent_c = 1;
    double chain_scale = 1;
    std::string seeding = "walk"; // walk or uniform
};

struct request_settings {
    uint32_t output_width, output_height;
    double cre, cim, scale;
    uint32_t lowr, lowg, lowb, highr, highg, highb;
    std::string sampler = "naive"; // naive or metropolis
    metropolis_settings metropolis;
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
    c.sampler = json.get<std::string>("sampler", c.sampler);
    metropolis_settings &m = c.metropolis;
    m.radius = json.get<double>("radius", m.radius);
    m.exponent_l = json.get<double>("exponent_l", m.exponent_l);
    m.exponent_c = json.get<double>("exponent_c", m.exponent_c);
    m.chain_scale = json.get<double>("chain_scale", m.chain_scale);
    m.seeding = json.get<std::string>("seeding", m.seeding);
    if (c.output_width == 0 || c.output_height == 0 ||
        uint64_t(c.output_width) * c.output_height > max_pixels)
        throw std::invalid_argument("viewport exceeds the two-million-pixel prototype limit");
    if (!std::isfinite(c.cre) || !std::isfinite(c.cim) || !std::isfinite(c.scale) || c.scale <= 0)
        throw std::invalid_argument("center and scale must be finite; scale must be positive");
    if (c.highr == 0 || c.highg == 0 || c.highb == 0 || c.highr > 100'000 || c.highg > 100'000 ||
        c.highb > 100'000 || c.lowr >= c.highr || c.lowg >= c.highg || c.lowb >= c.highb)
        throw std::invalid_argument("invalid channel iteration ranges");
    if (c.sampler != "naive" && c.sampler != "metropolis")
        throw std::invalid_argument("the sampler must be naive or metropolis");
    // The negated comparisons also reject NaN.
    if (!(m.radius > 0 && m.radius <= 100))
        throw std::invalid_argument("the mutation radius must be in (0, 100] percent");
    if (!(m.exponent_l >= 0 && m.exponent_l <= 8 && m.exponent_c >= 0 && m.exponent_c <= 8))
        throw std::invalid_argument("the density exponents must be in [0, 8]");
    if (!(m.chain_scale > 0 && m.chain_scale <= 100))
        throw std::invalid_argument("the chain length factor must be in (0, 100]");
    if (m.seeding != "walk" && m.seeding != "uniform")
        throw std::invalid_argument("seeding must be walk or uniform");
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
    s.sampler = c.sampler;
    s.exclusion = BUDDHA_EXCLUSION_MAP;
    s.no_image = true;
    s.indirect_settings();
    return s;
}

} // namespace buddha_browser

#endif
