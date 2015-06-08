
#include "settings.h"
#include "buddha.h"

#include <boost/gil/image.hpp>
#include <boost/gil/typedefs.hpp>
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
            if ( b->raw[j+0] > maxr ) maxr = b->raw[j+0];
            if ( b->raw[j+1] > maxg ) maxg = b->raw[j+1];
            if ( b->raw[j+2] > maxb ) maxb = b->raw[j+2];
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
        uint y = p.y < (s->h / 2) ? p.y : (s->h - p.y - 1);
        uint i = y * 3 * s->w + 3 * x + 0;


        int d = 16; // how to compute bit depth from result type????

        int rr = min( powf( b->raw[i+0], s->realContrast ) * rmul * (1 << (d/2)), (float) (1 << d) - 1 );
        int gg = min( powf( b->raw[i+1], s->realContrast ) * gmul * (1 << (d/2)), (float) (1 << d) - 1 );
        int bb = min( powf( b->raw[i+2], s->realContrast ) * bmul * (1 << (d/2)), (float) (1 << d) - 1 );
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
