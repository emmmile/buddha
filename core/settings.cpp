#include <settings.h>

void settings::indirect_settings() {
    rangere = w / scale;
    rangeim = h / scale;
    minre = cre - rangere * 0.5;
    maxre = cre + rangere * 0.5;
    minim = cim - rangeim * 0.5;
    maxim = cim + rangeim * 0.5;
    high = max(max(highr, highg), highb);
    low = min(min(lowr, lowg), lowb);
    symmetric_image = (cim == 0.0);
    histogram_height = symmetric_image ? h / 2 + h % 2 : h;
    size = w * histogram_height;

    realLightness = (float)lightness / (maxLightness - lightness + 1) * 0.5;
    realContrast = (float)contrast / (maxContrast) * 0.7;
}

void settings::dump() const {
    BOOST_LOG_TRIVIAL(debug) << "low: " << low << ", high " << high;
    BOOST_LOG_TRIVIAL(debug) << "cre: " << cre << ", cim " << cim;
    BOOST_LOG_TRIVIAL(debug) << "maxre: " << maxre << ", maxim " << maxim;
    BOOST_LOG_TRIVIAL(debug) << "minre: " << minre << ", minim " << minim;
    BOOST_LOG_TRIVIAL(debug) << "width: " << w << ", height: " << h;
    BOOST_LOG_TRIVIAL(debug) << "scale: " << scale << ", threads: " << threads;
    BOOST_LOG_TRIVIAL(debug) << "lightness: " << lightness << ", contrast: " << contrast;
    BOOST_LOG_TRIVIAL(debug) << "output name: " << outfile;
    BOOST_LOG_TRIVIAL(debug) << "write image: " << !no_image;
    BOOST_LOG_TRIVIAL(debug) << "checkpoint format: zstd";
    BOOST_LOG_TRIVIAL(debug) << "sampler: " << (sampler.empty() ? "default" : sampler);
}
