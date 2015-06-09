/*
 * Copyright (c) 2010, Emilio Del Tessandoro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY EMILIO DEL TESSANDORO o ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL EMILIO DEL TESSANDORO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#include "buddha_generator.h"
#include "timer.h"
#include "saver.h"


#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
//#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/extension/io/png_io.hpp>

namespace bar = boost::archive;
namespace bio = boost::iostreams;






buddha::buddha( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::buddha()";
    computed = 0;
}




void buddha::reduce ( ) {
    //swap(raw, generators[0]->raw);

    for ( auto i : generators )
        computed += i->computed;

    BOOST_LOG_TRIVIAL(info) << "buddha::reduce(), computed " << computed / 1000000.0 << " Mpoints in " << totaltime << " s";
    BOOST_LOG_TRIVIAL(info) << "buddha::reduce(), " << computed / totaltime / 1000000.0 << " Mpoints/s";
}




void buddha::save () {
    //BOOST_LOG_TRIVIAL(debug) << "buddha::save()";
    reduce();
    timer time;

    // save the raw histogram
    std::ofstream oss( s.outfile + ".gz", std::ios::binary);

    bio::filtering_stream<bio::output> f;
    f.push(bio::gzip_compressor());
    f.push(oss);
    bar::binary_oarchive oa(f);
    // oa << raw;
    for (pixel i : raw) oa << i;

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), compression: " << time.elapsed() << " s";

    time.restart();

    typedef rgb_view<rgb16_pixel_t> deref_t;
    typedef deref_t::point_t         point_t;
    typedef virtual_2d_locator<deref_t,false> locator_t;
    typedef image_view<locator_t> my_virt_view_t;

    boost::function_requires<PixelLocatorConcept<locator_t> >();
    gil_function_requires<StepIteratorConcept<locator_t::x_iterator> >();

    point_t dims(s.w, s.h);
    my_virt_view_t view(dims, locator_t(point_t(0,0), point_t(1,1), deref_t(this, &s, dims)));
    boost::gil::png_write_view( s.outfile + ".png", rotated90cw_view(view));

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), PNG: " << time.elapsed() << " s";
}


void buddha::load ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::load()";
    timer time;
    // save the raw histogram
    std::ifstream iss( s.infile, ios::in | ios::binary);

    bio::filtering_stream<bio::input> f;
    f.push(bio::gzip_decompressor());
    f.push(iss);
    bar::binary_iarchive ia(f);
    // ia >> generators[0]->raw;
    for ( unsigned long int i = 0; i < 3 * s.size; ++i )
        ia >> raw[i];

    BOOST_LOG_TRIVIAL(debug) << "buddha::load(), decompression: " << time.elapsed() << " s";
}


buddha::~buddha ( ) {
}




void buddha::clearBuffers ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::clearBuffers()";
}


void buddha::startGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::startGenerators()";

    // Block all signals for background s.threads
    sigset_t new_mask;
    sigfillset(&new_mask);
    sigset_t old_mask;
    pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

    for ( uint i = 0; i < s.threads; ++i ) {
        generators[i]->start( );
    }

    // Restore previous signals.
    pthread_sigmask(SIG_SETMASK, &old_mask, 0);
}


// stop the generators if they're running and if their status is different from STOP.
// XXX this can cause problems if a generator is in PAUSE, but for how the program is designed
// I think this is impossible.
// If the s.threads were running acquire completely the semaphore.
void buddha::stopGenerators ( ) {
    for ( uint i = 0; i < s.threads; ++i ) {
        lock_guard<mutex> locker ( generators[i]->execution );
        generators[i]->finish = true;
    }

    for ( uint i = 0; i < s.threads; ++i ) {
        generators[i]->t.join();
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha::stopGenerators()";
}



void buddha::run ( const settings& settings ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::run()";
    BOOST_LOG_TRIVIAL(info) << "sizeof(atomic): " << sizeof(sig_atomic_t);
    s = settings;
    s.dump( );

    raw.resize( 3 * s.size );
    raw.shrink_to_fit( );
    fill(raw.begin(),raw.end(), 0);

    for ( uint i = 0; i < 1024; ++i )
        rawmutex.emplace_back(new mutex());

    for ( uint i = 0; i < s.threads; ++i )
        generators.push_back( new buddha_generator( &s, this ) );

    clearBuffers();
    if ( s.infile != "" ) load( );


    timer time;
    startGenerators();

    // Wait for signal indicating time to shut down.
    sigset_t wait_mask;
    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGINT);
    sigaddset(&wait_mask, SIGQUIT);
    sigaddset(&wait_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
    int sig = 0;
    sigwait(&wait_mask, &sig);

    BOOST_LOG_TRIVIAL(debug) << "interrupt signal (" << sig << ") received";

    stopGenerators( );
    totaltime = time.elapsed();

    save( );
}
