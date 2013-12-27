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



#include "buddhaGenerator.h"
#include "staticStuff.h"
#include <math.h>
#include <float.h>
#include <iostream>
#include <stdio.h>
#include <thread>
#include <signal.h>
#include <boost/timer.hpp>




buddha::buddha( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::Buddha()";
    size = w = h = lowr = lowg = lowb = highr = highg = highb = 0;
    cre = cim = scale = 0.0;
    raw = NULL;
    RGBImage = NULL;
    threads = 0;
}



void buddha::createImage ( ) {
    //cout << "Buddha::toImage()\n";
    unsigned char r, g, b;
    uint j = 0;
    for ( uint i = 0; i < size; ++i, j += 3 ) {
        r = min( powf( raw[j + 0], realContrast ) * rmul, 255.0f );
        g = min( powf( raw[j + 1], realContrast ) * gmul, 255.0f );
        b = min( powf( raw[j + 2], realContrast ) * bmul, 255.0f );

        RGBImage[i] = r << 16 | g << 8 | b;
    }
}


// this function takes the raw data from generator i and sums it
// to the local raw array. The main difference is that if QTOPENCL is activated
// I have to use an ARGB array of ints instead the simple RGB array kept by
// every generator. This is because i get a lot of strange errors from the
// execution of the opencl kernel. I don't know if this is a bug but for the
// moment i keep this (useless) difference between using ARGB and RGB.
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
}


void buddha::reduce ( ) {
    memset( raw, 0, 3 * size * sizeof( int ) );

    for ( uint i = 0; i < threads; ++i )
        reduceStep( i, i == (threads - 1) );
}


void buddha::toRGB( ) {
    boost::timer time;
    reduce();
    int elapsed = time.elapsed();

    time.restart();
    createImage( );
    BOOST_LOG_TRIVIAL(info) << "Buddha::updateRGBImage(), reduce: " << elapsed << " ms, image build: " << time.elapsed() << " ms";
}

void buddha::save ( string& fileName ) {
    /*QImage out( (uchar*) RGBImage, w, h, QImage::Format_RGB32 );
    out.save( fileName, "PNG" );

    QByteArray compress = qCompress( (const uchar*) RGBImage, w * h * sizeof(int), 9 );
    //cout << "Compressed size vs Full: " << compress.size() << " " << w * h * sizeof(int) << endl;*/
}

buddha::~buddha ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::~Buddha()";
    free( raw );
    free( RGBImage );
}




void buddha::clearBuffers ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::clearBuffers()";
    memset( RGBImage, 0, size * sizeof( int ) );
    memset( raw, 0, 3 * size * sizeof( int ) );

    for ( uint i = 0; i < threads; ++i ) {
        lock_guard<mutex> locker( generators[i]->execution );
        // could be done also indirectly but it not so costly
        if ( generators[i]->raw ) memset( generators[i]->raw, 0, 3 * size * sizeof( int ) );
    }
}


void buddha::startGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::startGenerators()";

    for ( uint i = 0; i < threads; ++i ) {
        generators[i]->start( );
    }
}


// stop the generators if they're running and if their status is different from STOP.
// XXX this can cause problems if a generator is in PAUSE, but for how the program is designed
// I think this is impossible.
// If the threads were running acquire completely the semaphore.
void buddha::stopGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::stopGenerators()";

    /*for ( int i = 0; i < threads; ++i ) {
        //if ( generators[i]->isRunning() ) { TODO XXX <<-- come fare??
            lock_guard<mutex> locker( generators[i]->execution );
            if ( generators[i]->status != STOP )
                generators[i]->stop( );
        //}
    }

    if ( generatorsStatus == RUN )
        semaphore.acquire( threads );

    //emit stoppedGenerators( true );
    generatorsStatus = STOP;*/
}


void buddha::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::run()";

    rangere = w / scale;
    rangeim = h / scale;
    minre = cre - rangere * 0.5;
    maxre = cre + rangere * 0.5;
    minim = cim - rangeim * 0.5;
    maxim = cim + rangeim * 0.5;
    high = max( max( highr, highg ), highb );
    low = min( min(lowr, lowg), lowb);
    size = w * h;

    //dump( );

    raw = (pixel*) realloc( raw, size * 3 * sizeof( pixel ) );
    RGBImage = (uint*) realloc( RGBImage, size * sizeof( uint ) );

    for ( uint i = 0; i < threads; ++i )
        generators.push_back( new buddha_generator( this ) );


    startGenerators();

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    sleep( 10 ); // some kind of loop with a timer that saves
    stopGenerators( );
}


void buddha::dump ( ) {
    BOOST_LOG_TRIVIAL(debug) << "lowr: " << lowr << ", highr " << highr;
    BOOST_LOG_TRIVIAL(debug) << "lowg: " << lowg << ", highg " << highg;
    BOOST_LOG_TRIVIAL(debug) << "lowb: " << lowb << ", highb " << highb;
    BOOST_LOG_TRIVIAL(debug) << "cre: " << cre << ", cim " << cim;
    BOOST_LOG_TRIVIAL(debug) << "width: " << w << ", height: " << h;
    BOOST_LOG_TRIVIAL(debug) << "scale: " << scale << ", threads: " << threads;
}
