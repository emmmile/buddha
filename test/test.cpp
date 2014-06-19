
#include <iostream>
#include <chrono>
#include "buddha.h"
#include "buddha_generator.h"
#include "settings.h"
#include "random.h"
#define BOOST_TEST_DYN_LINK        // this is optional
#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_MODULE MyTest
#include <boost/test/unit_test.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace logging = boost::log;
using namespace std;


void init()
{
    logging::core::get()->set_filter
    (
        logging::trivial::severity >= logging::trivial::info
    );
}



BOOST_AUTO_TEST_CASE( distribution )
{
    double sum = 0.0;
    size_t trials = 100000;

    Random gen( std::chrono::system_clock::now().time_since_epoch().count() );
    for ( int i = 0; i < trials; ++i ) {
        sum += gen.realnegative();
    }

    double delta = 0.01;
    BOOST_CHECK( fabs(sum / trials) < delta );
}


BOOST_AUTO_TEST_CASE( start )
{
    init();

    buddha b;
    settings options( b, 0, NULL );
    b.indirect_settings();
    b.dump();

    size_t trials = 10;
    uint ok = 0;
    for ( int i = 0; i < trials; ++i ) {
        buddha_generator::complex_type begin( 0.0, 0.0 );
        double centerDistance;
        uint contribute;
        uint calculated;

        buddha_generator generator( &b );
        generator.findPoint( begin, centerDistance, contribute, calculated );

        if ( contribute != 0 ) ok++;
    }


    BOOST_CHECK( ok + 2 >= trials );
}


BOOST_AUTO_TEST_CASE( evaluate )
{
    init();

    buddha b;
    settings options( b, 0, NULL );
    b.indirect_settings();
    b.dump();

    size_t trials = 10;
    uint ok = 0;
    for ( int i = 0; i < trials; ++i ) {
        buddha_generator::complex_type begin( 0.0, 0.0 );
        double centerDistance;
        uint contribute;
        uint calculated;

        buddha_generator generator( &b );
        generator.findPoint( begin, centerDistance, contribute, calculated );

        if ( contribute != 0 ) ok++;
    }


    BOOST_CHECK( ok + 2 >= trials );
}
