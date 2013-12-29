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

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/extension/io/png_io.hpp>

namespace bar = boost::archive;
namespace bio = boost::iostreams;






buddha::buddha( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::buddha()";
    size = w = h = lowr = lowg = lowb = highr = highg = highb = 0;
    cre = cim = scale = 0.0;
    raw = NULL;
    rchannel = gchannel = bchannel = NULL;
    threads = 0;
    computed = 0;
}



void buddha::reduceStep ( int i, bool checkValues ) {
    uint j;
    if ( checkValues ) maxr = maxg = maxb = 0;

    lock_guard<mutex> lock( generators[i]->execution );

    for ( j = 0; j < 3 * size; j += 3 ) {
        raw[j+0] += generators[i]->raw[j+0];
        raw[j+1] += generators[i]->raw[j+1];
        raw[j+2] += generators[i]->raw[j+2];

        if ( checkValues ) {
            if ( raw[j+0] > maxr ) maxr = raw[j+0];
            if ( raw[j+1] > maxg ) maxg = raw[j+1];
            if ( raw[j+2] > maxb ) maxb = raw[j+2];
        }
    }

    if ( checkValues ) {
        rmul = maxr > 0 ? log( scale ) / (float) powf( maxr, realContrast ) * 150.0 * realLightness : 0.0;
        gmul = maxg > 0 ? log( scale ) / (float) powf( maxg, realContrast ) * 150.0 * realLightness : 0.0;
        bmul = maxb > 0 ? log( scale ) / (float) powf( maxb, realContrast ) * 150.0 * realLightness : 0.0;
    }

    computed += generators[i]->computed;
}


void buddha::reduce ( ) {
    //memset( raw, 0, 3 * size * sizeof( int ) );

    for ( uint i = 0; i < threads; ++i )
        reduceStep( i, i == (threads - 1) );


    BOOST_LOG_TRIVIAL(info) << "buddha::reduce(), computed " << computed << " points in " << totaltime << " s";
    BOOST_LOG_TRIVIAL(info) << "buddha::reduce(), " << computed / totaltime / 1000000.0 << " Mpoints/s";
}


void buddha::createImage ( ) {
    //cout << "buddha::toImage()\n";
    unsigned char r, g, b;
    uint j = 0;
    for ( uint i = 0; i < size; ++i, j += 3 ) {
        r = min( powf( raw[j + 0], realContrast ) * rmul, 255.0f );
        g = min( powf( raw[j + 1], realContrast ) * gmul, 255.0f );
        b = min( powf( raw[j + 2], realContrast ) * bmul, 255.0f );

        rchannel[i] = r;
        gchannel[i] = g;
        bchannel[i] = b;
        //RGBImage[i] = r << 16 | g << 8 | b;
    }
}

void buddha::toRGB( ) {
    buddha_timer time;
    reduce();
    double elapsed = time.elapsed() * 1000;


    time.restart();
    createImage( );
    BOOST_LOG_TRIVIAL(info) << "buddha::toRGB(), reduce: " << elapsed << " ms, image build: " << time.elapsed() * 1000 << " ms";
}

void buddha::save () {
    //BOOST_LOG_TRIVIAL(debug) << "buddha::save()";
    buddha_timer time;


    // save the image
    boost::gil::rgb8c_planar_view_t view = boost::gil::planar_rgb_view(w, h, rchannel, gchannel, bchannel, w);
    boost::gil::png_write_view( outfile + ".png", view);

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), PNG: " << time.elapsed() * 1000 << " ms";
    time.restart();



    // save the raw histogram
    std::ofstream oss( outfile + ".bz2", std::ios::binary);

    bio::filtering_stream<bio::output> f;
    f.push(bio::bzip2_compressor());
    f.push(oss);
    bar::binary_oarchive oa(f);
    vector<pixel> tmp( 3 * size );
    copy( raw, raw +  3 * size, tmp.begin() );
    oa << tmp;

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), compression: " << time.elapsed() * 1000 << " ms";
}


void buddha::load ( ) {
    buddha_timer time;
    // save the raw histogram
    std::ifstream iss( infile, ios::in | ios::binary);

    bio::filtering_stream<bio::input> f;
    f.push(bio::bzip2_decompressor());
    f.push(iss);
    bar::binary_iarchive ia(f);
    vector<pixel> tmp( 3 * size );
    ia >> tmp;
    copy( tmp.begin(), tmp.end(), raw );

    BOOST_LOG_TRIVIAL(debug) << "buddha::load(), decompression: " << time.elapsed() * 1000 << " ms";
}


buddha::~buddha ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::~buddha()";
    free( raw );
    free( rchannel );
    free( gchannel );
    free( bchannel );
}




void buddha::clearBuffers ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::clearBuffers()";
    memset( rchannel, 0, size * sizeof( uchar ) );
    memset( gchannel, 0, size * sizeof( uchar ) );
    memset( bchannel, 0, size * sizeof( uchar ) );
    memset( raw, 0, 3 * size * sizeof( pixel ) );

    for ( uint i = 0; i < threads; ++i ) {
        lock_guard<mutex> locker( generators[i]->execution );
        // could be done also indirectly but it not so costly
        if ( generators[i]->raw ) memset( generators[i]->raw, 0, 3 * size * sizeof( pixel ) );
    }
}


void buddha::startGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::startGenerators()";

    // Block all signals for background threads
    sigset_t new_mask;
    sigfillset(&new_mask);
    sigset_t old_mask;
    pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

    for ( uint i = 0; i < threads; ++i ) {
        generators[i]->start( );
    }

    // Restore previous signals.
    pthread_sigmask(SIG_SETMASK, &old_mask, 0);
}


// stop the generators if they're running and if their status is different from STOP.
// XXX this can cause problems if a generator is in PAUSE, but for how the program is designed
// I think this is impossible.
// If the threads were running acquire completely the semaphore.
void buddha::stopGenerators ( ) {
    for ( uint i = 0; i < threads; ++i ) {
        lock_guard<mutex> locker ( generators[i]->execution );
        generators[i]->finish = true;
    }

    for ( uint i = 0; i < threads; ++i ) {
        generators[i]->t.join();
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha::stopGenerators()";
}


void buddha::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::run()";

    rangere = w / scale;
    rangeim = h / scale;
    minre = cre - rangere * 0.5;
    maxre = cre + rangere * 0.5;
    minim = cim - rangeim * 0.5;
    maxim = cim + rangeim * 0.5;
    high = max( max( highr, highg ), highb );
    low = min( min(lowr, lowg), lowb);
    size = w * h;

    realLightness = (float) lightness / ( maxLightness - lightness + 1 );
    realContrast = (float) contrast / maxContrast * 2.0;

    dump( );

    raw = (pixel*) realloc( raw, size * 3 * sizeof( pixel ) );
    rchannel = (uchar*) realloc( rchannel, size * sizeof( uchar ) );
    gchannel = (uchar*) realloc( gchannel, size * sizeof( uchar ) );
    bchannel = (uchar*) realloc( bchannel, size * sizeof( uchar ) );

    for ( uint i = 0; i < threads; ++i )
        generators.push_back( new buddha_generator( this ) );

    clearBuffers();
    if ( infile != "" ) load( );


    buddha_timer time;
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

    toRGB();
    save( );
}


void buddha::dump ( ) {
    BOOST_LOG_TRIVIAL(debug) << "lowr: " << lowr << ", highr " << highr;
    BOOST_LOG_TRIVIAL(debug) << "lowg: " << lowg << ", highg " << highg;
    BOOST_LOG_TRIVIAL(debug) << "lowb: " << lowb << ", highb " << highb;
    BOOST_LOG_TRIVIAL(debug) << "cre: " << cre << ", cim " << cim;
    BOOST_LOG_TRIVIAL(debug) << "width: " << w << ", height: " << h;
    BOOST_LOG_TRIVIAL(debug) << "scale: " << scale << ", threads: " << threads;
    BOOST_LOG_TRIVIAL(debug) << "lightness: " << lightness << ", contrast: " << contrast;
}
