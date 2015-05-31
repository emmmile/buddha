
#include "buddha.h"
#include "buddha_generator.h"
#include "settings.h"
#include "timer.h"

#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>

int main ( int argc, char** argv ) {
    // uses this only as container for the program parameters
    buddha b;
    settings options( b, argc, argv );

    uint computed = 0;
    double totaltime;

    //b.w = b.w / 10;
    //b.h = b.h / 10;
    //b.scale = b.scale / 10;
    b.highr = b.highg = b.highb = 8192 * 16;


    // now start a generator's evaluate() manually
    b.indirect_settings();
    //b.dump();
    vector<buddha_generator::complex_type> starting_points;
    double distance;
    uint contribute;
    uint calculated;
    starting_points.emplace_back( -0.148409, +0.835417 );
    starting_points.emplace_back( +0.148409, +0.835417 );
    starting_points.emplace_back( +0.148409, -0.835417 );
    starting_points.emplace_back( -0.148409, -0.835417 );

    starting_points.emplace_back( +0.292942, +0.450511 );
    starting_points.emplace_back( -0.292942, +0.450511 );
    starting_points.emplace_back( +0.292942, -0.450511 );
    starting_points.emplace_back( -0.292942, -0.450511 );

    starting_points.emplace_back( -0.767293, -0.0913622 );
    starting_points.emplace_back( +0.767293, -0.0913622 );
    starting_points.emplace_back( -0.767293, +0.0913622 );
    starting_points.emplace_back( +0.767293, +0.0913622 );
//    starting_points.emplace_back( -0.328038, -0.613444 );
//    starting_points.emplace_back( -0.357572, -0.601857 );
//    starting_points.emplace_back( -0.261254, 0.633443 );
//    starting_points.emplace_back( -0.0909596, 0.648495 );
//    starting_points.emplace_back( -0.749504, -0.0253876 );


    buddha_generator benchmark( &b );

    timer time;
    for ( uint i = 0; i < 1000; ++i ) {
        for ( auto begin : starting_points ) {
            benchmark.evaluate( begin, distance, contribute, calculated );
            computed += calculated;
        }
    }
    totaltime = time.elapsed();


    BOOST_LOG_TRIVIAL(info) << "computed " << computed << " points in " << totaltime << " s";
    BOOST_LOG_TRIVIAL(info) << "" << computed / totaltime / 1000000.0 << " Mpoints/s";

    //assert( 786702000 == computed );

    return 0;
}

