#ifndef SETTINGS_H
#define SETTINGS_H

#include <complex>
#include <cstdint>
#include <fstream>
#include <string>
#ifndef BOOST_LOG_DYN_LINK
#define BOOST_LOG_DYN_LINK
#endif
#include <boost/log/trivial.hpp>

#include "buddha_kernel.h"
using namespace std;

typedef unsigned char uchar;

struct settings {
    // since this class is also used as "container" for the various generators
    // I use directly public variables instead private members and functions like set*()
    // buddhabrot characteristics
    double maxre, maxim;
    double minre, minim;
    double cre, cim;
    uint32_t low;
    uint32_t lowr;
    uint32_t lowg;
    uint32_t lowb;
    uint32_t high;
    uint32_t highr;
    uint32_t highg;
    uint32_t highb;
    double scale;

    // these can be calculated from the previous but they are useful
    double rangere, rangeim;
    uint64_t w;
    uint64_t h;
    uint64_t size;
    uint64_t histogram_height;
    bool symmetric_image;

    uint32_t contrast, lightness;
    float realContrast, realLightness;

    static const uint32_t maxLightness = 200;
    static const uint32_t maxContrast = 200;

    string outfile;
    string infile;
    bool no_image;
    bool allow_legacy_checkpoint;
    uint32_t threads;
    bool inverse;

    // "metropolis" or "naive"; empty selects the renderer's default (buddha++: metropolis,
    // buddha-metal: naive, the only one it supports). Checkpoints record it.
    string sampler;
    string exclusion;
    uint64_t exclusion_size;

    void indirect_settings();
    void dump() const;

    // Histogram window, as drawn by both renderers.
    buddha_kernel::geometry<double> histogram_geometry() const {
        buddha_kernel::geometry<double> g;
        g.minre = minre;
        g.maxim = maxim;
        g.scale = scale;
        g.width = uint32_t(w);
        g.height = uint32_t(histogram_height);
        g.symmetric = symmetric_image;
        g.odd_center = symmetric_image && h % 2 != 0;
        return g;
    }

    // Naive-sampler parameters, shared by buddha_generator::naive and buddha-metal. The caller
    // sets the sample range, random key and lane count.
    buddha_kernel::parameters kernel_parameters() const {
        const buddha_kernel::geometry<double> g = histogram_geometry();
        buddha_kernel::parameters p{};
        p.low = low;
        p.high = high;
        p.lowr = lowr;
        p.highr = highr;
        p.lowg = lowg;
        p.highg = highg;
        p.lowb = lowb;
        p.highb = highb;
        p.width = g.width;
        p.histogram_height = g.height;
        p.symmetric = g.symmetric;
        p.odd_center = g.odd_center;
        p.exclusion_size = uint32_t(exclusion_size);
        p.minre = float(g.minre);
        p.maxim = float(g.maxim);
        p.scale = float(g.scale);
        p.threads = 1;
        return p;
    }
};

#endif
