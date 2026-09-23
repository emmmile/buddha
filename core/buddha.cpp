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
#include "saver.h"


#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
//#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>
#include "timer.h"

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/extension/io/png/old.hpp>

namespace bar = boost::archive;
namespace bio = boost::iostreams;

namespace {
uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint64_t generator_seed(uint64_t base_seed, uint64_t stream) {
    if (base_seed != 0)
        return splitmix64(base_seed + stream);

    random_device random;
    return (uint64_t(random()) << 32) | random();
}
}




buddha::buddha( const settings& s ) : core(s), s(s), computed(0) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::buddha()";

    s.dump( );

    raw.reserve(3 * s.size);
    for ( uint64_t i = 0; i < 3 * s.size; ++i )
        raw.emplace_back(0);

    for ( unsigned int i = 0; i < s.threads; ++i )
        generators.push_back( new buddha_generator( core, raw, s, generator_seed(s.seed, i) ) );

    clearBuffers();
    if ( s.exclusion != "" ) core.load();
    if ( s.infile != "" ) load( );
}




void buddha::reduce ( ) {
    //swap(raw, generators[0]->raw);

    unsigned long long find_attempts = 0;
    unsigned long long proposals = 0;
    unsigned long long accepted = 0;
    unsigned long long drawn_orbits = 0;
    for ( auto i : generators ) {
        computed += i->computed;
        find_attempts += i->find_attempts;
        proposals += i->proposals;
        accepted += i->accepted;
        drawn_orbits += i->drawn_orbits;
    }

    unsigned long long int total = 0;
    for ( auto i : raw ) total += i.load();

    
    BOOST_LOG_TRIVIAL(info) << "computed " << computed / 1000000.0 << " Mpoints in " 
                            << totaltime << " s (" << computed / totaltime / 1000000.0 << " Mpoints/s)";

    BOOST_LOG_TRIVIAL(info) << total / 1000000.0 << " Mpoints in the histogram ("
                            << total / totaltime / 1000000.0 << " Mpoints/s)";
    BOOST_LOG_TRIVIAL(info) << "find attempts: " << find_attempts
                            << ", proposals: " << proposals
                            << ", accepted: " << accepted
                            << " (" << (proposals ? double(accepted) / proposals : 0.0) << ")"
                            << ", drawn orbits: " << drawn_orbits;
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
    for (auto i : raw) {
        pixel current = i.load();
        oa << current;
    }

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), compression: " << time.elapsed() << " s";

    time.restart();

    if (s.output_format == "png") {
        typedef rgb_view<rgb16_pixel_t> deref_t;
        typedef deref_t::point_t         point_t;
        typedef virtual_2d_locator<deref_t,false> locator_t;
        typedef image_view<locator_t> my_virt_view_t;

        boost::function_requires<PixelLocatorConcept<locator_t> >();
        gil_function_requires<StepIteratorConcept<locator_t::x_iterator> >();

        point_t dims(s.w, s.h);
        my_virt_view_t view(dims, locator_t(point_t(0,0), point_t(1,1), deref_t(this, &s, dims)));
        boost::gil::png_write_view( s.outfile + ".png", rotated90cw_view(view));
    } else if (s.output_format == "tiff") {
        write_tiff(this, &s, s.outfile + ".tiff");
    } else {
        throw runtime_error("unknown output format: " + s.output_format);
    }

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), " << s.output_format << ": " << time.elapsed() << " s";
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
    for ( uint64_t i = 0; i < 3 * s.size; ++i ) {
        pixel current;
        ia >> current;
        raw[i].store(current);
    }

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

    for ( unsigned int i = 0; i < s.threads; ++i ) {
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
    for ( unsigned int i = 0; i < s.threads; ++i ) {
        lock_guard<mutex> locker ( generators[i]->execution );
        generators[i]->finish = true;
    }

    for ( unsigned int i = 0; i < s.threads; ++i ) {
        generators[i]->t.join();
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha::stopGenerators()";
}


void buddha::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::run()";

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

    if ( getenv("BUDDHA_BENCHMARK_NO_SAVE") ) {
        reduce();
        return;
    }

    save( );
}
