#include "buddha.h"
#include "settings.h"
#include "settings_parser.h"
#include "timer.h"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
//#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/extension/io/png_io.hpp>
#include <boost/gil/image.hpp>
#include <boost/gil/typedefs.hpp>
//#include <boost/gil/extension/numeric/sampler.hpp>
//#include <boost/gil/extension/numeric/resample.hpp>

namespace bar = boost::archive;
namespace bio = boost::iostreams;


using namespace std;

unsigned int maxr = 0, maxg = 0, maxb = 0;

void precomputation( char** argv, settings& s ) {
    std::ifstream riss( argv[1], ios::in | ios::binary);
    bio::filtering_stream<bio::input> rf;
    rf.push(bio::gzip_decompressor());
    rf.push(riss);
    bar::binary_iarchive ria(rf);

    buddha::pixel r, g, b;
    // ia >> generators[0]->raw;
    for ( unsigned long int i = 0; i < 3 * s.size; ++i ) {
    	ria >> r;
    	gia >> g;
    	bia >> b;

    	// precomputation
    	maxr = max(r, maxr);
    	maxg = max(g, maxg);
    	maxb = max(b, maxb);
    }
}


int main ( int argc, char** argv ) {
    settings_parser parser( argc, argv );
    settings s = parser();

    timer time;

    vector<unsigned char> rchannel;
    vector<unsigned char> gchannel;
    vector<unsigned char> bchannel;

    std::ifstream riss( argv[1], ios::in | ios::binary);
    bio::filtering_stream<bio::input> rf;
    rf.push(bio::gzip_decompressor());
    rf.push(riss);
    bar::binary_iarchive ria(rf);

    std::ifstream giss( argv[2], ios::in | ios::binary);
    bio::filtering_stream<bio::input> gf;
    gf.push(bio::gzip_decompressor());
    gf.push(giss);
    bar::binary_iarchive gia(gf);

    std::ifstream biss( argv[3], ios::in | ios::binary);
    bio::filtering_stream<bio::input> bf;
    bf.push(bio::gzip_decompressor());
    bf.push(biss);
    bar::binary_iarchive bia(bf);


    precomputation(argv, s);
    BOOST_LOG_TRIVIAL(info) << "precomputation: " << time.elapsed() * 1000 << " ms";
    time.restart();


    buddha::pixel r, g, b;
    float rmul = maxr > 0 ? log( s.scale ) / (float) powf( maxr, s.realContrast ) * 150.0 * s.realLightness : 0.0;
    float gmul = maxg > 0 ? log( s.scale ) / (float) powf( maxg, s.realContrast ) * 150.0 * s.realLightness : 0.0;
    float bmul = maxb > 0 ? log( s.scale ) / (float) powf( maxb, s.realContrast ) * 150.0 * s.realLightness : 0.0;

    for ( uint i = 0; i < s.size; i++ ) {
    	ria >> r;
    	gia >> g;
    	bia >> b;

        r = min( powf( r, s.realContrast ) * rmul, 255.0f );
        g = min( powf( g, s.realContrast ) * gmul, 255.0f );
        b = min( powf( b, s.realContrast ) * bmul, 255.0f );

        rchannel.push_back(r);
        gchannel.push_back(g);
        bchannel.push_back(b);
    }

    time.restart();

    // save the image
    boost::gil::rgb8c_planar_view_t view = 
        boost::gil::planar_rgb_view(s.w, s.h, 
            &rchannel[0], &gchannel[0], &bchannel[0], s.w);


    /*rgb8_image_t preview(s.w / 20, s.h / 20);
    resize_view(const_view(view), view(preview), bilinear_sampler());*/

    boost::gil::png_write_view(s.outfile + ".png", view);

    BOOST_LOG_TRIVIAL(info) << "PNG: " << time.elapsed() * 1000 << " ms";
	

    return 0;
}