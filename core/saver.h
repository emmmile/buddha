
#include "settings.h"
#include "buddha.h"

#include <boost/gil/image.hpp>
#include <boost/gil/typedefs.hpp>
#include <tiffio.h>
#include <vector>
using namespace boost::gil;



// use buddha::raw as input and produce RGB pixels
template <typename P>   // Models PixelValueConcep
struct rgb_view {
    typedef point2<ptrdiff_t>   point_t;

    typedef rgb_view            const_t;
    typedef P                   value_type;
    typedef value_type          reference;
    typedef value_type          const_reference;
    typedef point_t             argument_type;
    typedef reference           result_type;
    BOOST_STATIC_CONSTANT(bool, is_mutable=false);

    rgb_view() {}
    rgb_view(buddha* b, settings* s, const point_t& sz) : _img_size(sz), b(b), s(s), maxr(0),maxg(0),maxb(0) {
        for ( size_t j = 0; j < 3 * s->size; j += 3 ) {
            if ( b->raw[j+0].load() > maxr ) maxr = b->raw[j+0].load();
            if ( b->raw[j+1].load() > maxg ) maxg = b->raw[j+1].load();
            if ( b->raw[j+2].load() > maxb ) maxb = b->raw[j+2].load();
        }

        BOOST_LOG_TRIVIAL(info) << "maximum red channel:   " << maxr;
        BOOST_LOG_TRIVIAL(info) << "maximum green channel: " << maxg;
        BOOST_LOG_TRIVIAL(info) << "maximum blue channel:  " << maxb;

        rmul = maxr > 0 ? log( s->scale ) / (float) powf( maxr, s->realContrast ) * 70.0 * s->realLightness : 0.0;
        gmul = maxg > 0 ? log( s->scale ) / (float) powf( maxg, s->realContrast ) * 70.0 * s->realLightness : 0.0;
        bmul = maxb > 0 ? log( s->scale ) / (float) powf( maxb, s->realContrast ) * 70.0 * s->realLightness : 0.0;
        //rmul = 1.0 / maxr;
        //gmul = 1.0 / maxg;
        //bmul = 1.0 / maxb;
    }

    result_type operator()(const point_t& p) const {
        uint x = p.x;
        uint y = p.y < int(s->h / 2) ? p.y : (s->h - p.y - 1);
        uint i = y * 3 * s->w + 3 * x + 0;


        int d = 16; // how to compute bit depth from result type????

        int rr = min( powf( b->raw[i+0].load(), s->realContrast ) * rmul * (1 << (d/2)), (float) (1 << d) - 1 );
        int gg = min( powf( b->raw[i+1].load(), s->realContrast ) * gmul * (1 << (d/2)), (float) (1 << d) - 1 );
        int bb = min( powf( b->raw[i+2].load(), s->realContrast ) * bmul * (1 << (d/2)), (float) (1 << d) - 1 );
        //int rr = min( pow(b->raw[i + 0] * rmul, 0.8) * (1 << d) * 2, (double) (1 << d) - 1);
        //int gg = min( pow(b->raw[i + 1] * gmul, 0.8) * (1 << d) * 2, (double) (1 << d) - 1);
        //int bb = min( pow(b->raw[i + 2] * bmul, 0.8) * (1 << d) * 2, (double) (1 << d) - 1);
        return result_type(rr, gg, bb);
    }
private:
    point_t _img_size;
    buddha* b;
    settings* s;

    uint32_t maxr;
    uint32_t maxg;
    uint32_t maxb;
    float rmul;
    float bmul;
    float gmul;
};

inline void write_tiff(buddha* b, settings* s, const string& filename) {
    typedef rgb_view<rgb16_pixel_t> deref_t;
    typedef deref_t::point_t point_t;

    // A 16-bit RGB raster takes six bytes per output pixel.  Choose BigTIFF
    // before the classic TIFF 4 GiB offset limit can be reached.
    uint64_t uncompressed_size = s->w * s->h * 3 * sizeof(uint16_t);
    bool bigtiff = uncompressed_size > uint64_t(UINT32_MAX);
    TIFF* out = TIFFOpen(filename.c_str(), bigtiff ? "w8" : "w");
    if (!out) throw runtime_error("unable to open TIFF output: " + filename);

    // Preserve the established 90-degree clockwise output orientation.
    uint32_t width = static_cast<uint32_t>(s->h);
    uint32_t height = static_cast<uint32_t>(s->w);
    bool configured =
        TIFFSetField(out, TIFFTAG_IMAGEWIDTH, width) &&
        TIFFSetField(out, TIFFTAG_IMAGELENGTH, height) &&
        TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3) &&
        TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16) &&
        TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) &&
        TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
        TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) &&
        TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE) &&
        TIFFSetField(out, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL) &&
        TIFFSetField(out, TIFFTAG_ZIPQUALITY, 1) &&
        TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, 32);
    if (!configured) {
        TIFFClose(out);
        throw runtime_error("unable to configure TIFF output: " + filename);
    }

    deref_t renderer(b, s, point_t(s->w, s->h));
    vector<uint16_t> row(size_t(width) * 3);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            rgb16_pixel_t pixel = renderer(point_t(y, s->h - x - 1));
            row[3 * x + 0] = at_c<0>(pixel);
            row[3 * x + 1] = at_c<1>(pixel);
            row[3 * x + 2] = at_c<2>(pixel);
        }
        if (TIFFWriteScanline(out, row.data(), y, 0) < 0) {
            TIFFClose(out);
            throw runtime_error("unable to write TIFF output: " + filename);
        }
    }
    TIFFClose(out);
}
