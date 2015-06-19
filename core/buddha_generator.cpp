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

#include <random>
#include "buddha_generator.h"
#include "timer.h"
#define METTHD		16000

using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif




buddha_generator::~buddha_generator ( ) {
    //BOOST_LOG_TRIVIAL(debug) << "buddha_generator::~buddha_generator";
}

buddha_generator::buddha_generator ( mandelbrot<complex_type>& core, vector_type& raw, const settings& s ) 
    : core(core), raw(raw), s(s) {
    random_device rd;
    unsigned int seed = rd();
    generator.seed( seed );

    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::initialize() with seed " << seed;

    seq.resize( s.high + 1 );
    next_point = s.next_point;

    finish = false;
    computed = 0;
}

void buddha_generator::start ( ) {
    t = thread (&buddha_generator::run, this);
}

void buddha_generator::stop ( ) {
    lock_guard<mutex> locker( execution );
    finish = true;
}






void buddha_generator::drawPoint ( complex_type& c, bool drawr, bool drawg, bool drawb ) {
    unsigned int x;
    unsigned int y;

#define plotIm( c, drawr, drawg, drawb ) \
    if ( c.imag() > s.minim && c.imag() < s.maxim ) { \
    y = ( s.maxim - fabs(c.imag()) ) * s.scale; \
    uint64_t i = y * 3 * s.w + 3 * x; \
    if ( drawr )    ++(raw[ i + 0 ]);  \
    if ( drawg )    ++(raw[ i + 1 ]);  \
    if ( drawb )    ++(raw[ i + 2 ]);  \
}

    if ( c.real() < s.minre ) return;
    if ( c.real() > s.maxre ) return;

    x = ( c.real() - s.minre ) * s.scale;
    //if ( x >= s.w ) return; // activate in case of problems

    // the y coordinates are referred to the point (s.minre, s.maxim), and are symetric in
    // respect of the real axis (re = 0). So I draw always also the simmetric point (I try).
    plotIm( c, drawr, drawg, drawb );
}


inline void buddha_generator::gaussianMutation ( complex_type& z, double radius ) {
    double redev, imdev;
    generator.gaussian( redev, imdev, radius );
    z = z + complex_type( redev, imdev );
}

inline void buddha_generator::exponentialMutation ( complex_type& z, double radius ) {
    double redev, imdev;
    generator.exponential( redev, imdev, radius );
    z = z + complex_type( redev, imdev );
}


// search for a point that falls in the screen, simply moves randomly making moves
// proportional in size to the distance from the center of the screen.
// I think can be optimized a lot
int buddha_generator::findPoint ( complex_type& begin, unsigned int& contribute, unsigned int& calculated ) {
    int max, iterations = 0;
    unsigned int calculatedInThisIteration;
    complex_type tmp = begin;

    // 64 - 512
#define FINDPOINTMAX 	256

    calculated = 0;
    do {
        gaussianMutation( tmp, 1.0 );
        seq[0] = tmp;

        max = core.evaluate( seq, contribute, calculatedInThisIteration, s );
        calculated += calculatedInThisIteration;

        begin = tmp;
    } while ( !contribute && ++iterations < FINDPOINTMAX );

    //BOOST_LOG_TRIVIAL(info) << iterations;
    return max;
}


// the metropolis algorithm. I don't know very much about the teory under this optimization but I think is
// implemented quite well.. Maybe a better method for the transition probability can be found but I don't know.
void buddha_generator::metropolis ( ) {
    complex_type begin( 0.0, 0.0 );
    unsigned int calculated, selectedOrbitCount = 0, proposedOrbitCount = 0;
    int selectedOrbitMax = 0, proposedOrbitMax = 0, j;
    double radius = 40.0 / s.scale; // 100.0;

    // search a point that has some contribute in the interested area
    selectedOrbitMax = findPoint( begin, selectedOrbitCount, calculated );
    computed += calculated;
    //cout << selectedOrbitMax << endl;

    // if the search failed I exit
    if ( selectedOrbitCount == 0 ) return;

    complex_type ok = begin;
    // also "how much" cicles are executed on each point is crucial. In order to have more points on the
    // screen an high iteration count could be better but, not too high because otherwise the space
    // is not sampled well. I tried values between 512 and 8192 and they works well. Over 80000 it becames strange.
    // Now i'm using something proportional on "how much the point is important".. For example how long the sequence
    // is and how many points falls on the window.
    for ( j = 0; j < max( (int) selectedOrbitCount * 256, selectedOrbitMax * 2 ); j++ ) {
        begin = ok;
        // the radius of the mutations influences a lot the quality of the rendering AND the speed.
        // I think that choose a random radius is the best way otherwise I noticed some geometric artifacts
        // around the point (-1.8, 0) for example. This artifacts however depend also on the number of iterations
        // explained above.

        //seq[0].mutate( random( &buf ) * radius /* + add */, &buf );
        exponentialMutation( begin, generator.real() * radius );
        seq[0] = begin;

        // calculate the new sequence
        proposedOrbitMax = core.evaluate( seq, proposedOrbitCount, calculated, s );

        // the sequence is periodic, I try another mutation
        if ( proposedOrbitMax <= 0 ) continue;

        // maybe the sequence is not periodic but It doesn't contribute on the actual region
        if ( proposedOrbitCount == 0 ) continue;


        // calculus of the transitional probability. One point is more probable of being
        // chose if generates a lot of points in the window
        double alpha =  proposedOrbitMax * proposedOrbitMax * proposedOrbitCount /
                double( selectedOrbitMax * selectedOrbitMax * selectedOrbitCount );


        if ( alpha > generator.real() ) {
            ok = begin;
            selectedOrbitCount = proposedOrbitCount;
            selectedOrbitMax = proposedOrbitMax;
        }

        computed += calculated;

        // draw the points
        lock_guard<mutex> locker( execution );

        for ( unsigned int i = s.low; int(i) <= proposedOrbitMax && proposedOrbitCount > 0 && i < s.high; i++ ) {
            drawPoint( seq[i], 
                       i < s.highr && i > s.lowr, 
                       i < s.highg && i > s.lowg, 
                       i < s.highb && i > s.lowb
            );
        }

        if ( finish ) break;
    }
}




// normal buddhabrot drawing function, no metropolis
void buddha_generator::normal ( ) {
    // normal uniform search
    seq[0] = complex_type( generator.real2negative() * 2, generator.real2negative() * 2 );
    unsigned int calculated;
    int orbitMax;

    // generate a random point
    //gaussianMutation( begin, 1.0 );
    //BOOST_LOG_TRIVIAL(info) << begin.real() << " " << begin.imag();
    // calculate the sequence
    orbitMax = core.evaluate( seq, calculated );
    computed += calculated;

    for ( int h = 0; h <= orbitMax - (int) s.low && h <= int(s.high - s.low); h++ ) {
        unsigned int i = h + s.low;
        drawPoint( seq[h], i < s.highr && i > s.lowr, i < s.highg && i > s.lowg, i < s.highb && i > s.lowb);
    }

    /*for ( unsigned int i = s.low; i <= orbitMax && i < s.high; i++ ) {
        drawPoint( seq[i], 
                   i < s.highr && i > s.lowr, 
                   i < s.highg && i > s.lowg, 
                   i < s.highb && i > s.lowb
        );
    }*/
}


void buddha_generator::test_exclusion ( ) {
    static unsigned int samples = 0;
    static unsigned int errors = 0;
    unsigned int calculated;

    for ( unsigned int i = 0; i < 1000000; i++ ) {
        seq[0] = complex_type( generator.real2negative() * 2, generator.real2negative() * 2 );
        if (core.evaluate(seq, calculated ) == -1 && core.evaluate(seq) != -1) {
            errors++;
            core.data[core.index(seq[0])] = false;
        }
    }

    samples += 1000000;
    BOOST_LOG_TRIVIAL(info) << errors << " errors on " << samples << " samples (" << double(errors) / samples << ")";
}


void buddha_generator::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run()";

    while ( true ) {
        //metropolis( );
        //normal( );
        test_exclusion( );

        lock_guard<mutex> locker ( execution );
        if ( finish ) break;
    }


    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run(), finished";
}
