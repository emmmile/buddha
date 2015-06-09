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


static constexpr unsigned int step = 32;


buddha_generator::buddha_generator () {
    finish = false;
}

buddha_generator::buddha_generator (settings *b, buddha* bu ) {
    initialize( b, bu );
}


buddha_generator::~buddha_generator ( ) {
    //BOOST_LOG_TRIVIAL(debug) << "buddha_generator::~buddha_generator";
}

void buddha_generator::initialize ( settings* b, buddha* bu ) {
    this->b = b;
    this->bu = bu;

    random_device rd;
    seed = rd();

    //buf.state = (int32_t*) statebuf; // this fixes the segfault
    //initstate_r( seed, statebuf, sizeof( statebuf ), &buf );
    generator.seed( seed );

    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::initialize() with seed " << seed;

    //raw = (buddha::pixel*) realloc( raw, 3 * b->size * sizeof( buddha::pixel ) );
    //memset( raw, 0, 3 * b->size * sizeof( buddha::pixel ) );
    //raw.resize( 3 * b->size );
    //raw.shrink_to_fit( );
    seq.resize( b->high - b->low );
    next_point = b->next_point;

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
    if ( c.imag() > b->minim && c.imag() < b->maxim ) { \
    y = ( b->maxim - fabs(c.imag()) ) * b->scale; \
    unsigned int i = y * 3 * b->w + 3 * x; \
    unsigned int xblock = x * bu->blocks / b->w; \
    unsigned int yblock = y * bu->blocks / b->h; \
    bu->rawmutex[yblock * bu->blocks + xblock]->lock(); \
    if ( drawr )    bu->raw[ i + 0 ]++;  \
    if ( drawg )    bu->raw[ i + 1 ]++;  \
    if ( drawb )    bu->raw[ i + 2 ]++;  \
    bu->rawmutex[yblock * bu->blocks + xblock]->unlock(); \
}

    if ( c.real() < b->minre ) return;
    if ( c.real() > b->maxre ) return;

    x = ( c.real() - b->minre ) * b->scale;
    //if ( x >= b->w ) return; // activate in case of problems

    // the y coordinates are referred to the point (b->minre, b->maxim), and are symetric in
    // respect of the real axis (re = 0). So I draw always also the simmetric point (I try).
    plotIm( c, drawr, drawg, drawb );
}



// test if a point is inside the interested area
int buddha_generator::inside ( complex_type& c ) {
    return  c.real() <= b->maxre &&
            c.real() >= b->minre &&
            ( ( c.imag() <= b->maxim && c.imag() >= b->minim ) ||
              ( -c.imag() <= b->maxim && -c.imag() >= b->minim ) );

    //return  c.real() <= b->maxre && c.real() >= b->minre && c.imag() <= b->maxim && c.imag() >= b->minim ;
}








// this is the main function. Here little modifications impacts a lot on the speed of the program!
int buddha_generator::evaluate ( complex_type& begin, double& centerDistance,
                                 unsigned int& contribute, unsigned int& calculated ) {
    complex_type last = begin;	// holds the last calculated point
    complex_type critical = last;// for periodicity check
    unsigned int j = 0, criticalStep = step;
    bool orbit_inside = false;
    centerDistance = 64.0;
    contribute = 0;

    // XXX this loop whould benefit a lot of removal of the first if

    for ( unsigned int i = 0; i < b->high; ++i ) {
        if ( i >= b->low ) seq[j++] = last;

        // this checks if the last point is inside the screen
        orbit_inside = orbit_inside || inside(last);
        if ( inside( last ) ) {
            
            centerDistance = 0.0;
            ++contribute;
        } else {

            // if we didn't passed inside the screen calculate the distance
            // it will update after the variable centerDistance
            double distance = norm( last - complex_type( b->cre, b->cim ) );
            if ( distance < centerDistance && norm( last ) < 4.0 ) centerDistance = distance;
        }

        // test the stop condition and eventually continue a little bit
        if ( norm( last ) > 8.0 ) {
            calculated = i;
            return i - 1;
        }

        if ( i == criticalStep ) {
            critical = last;
        } else if ( i > criticalStep ) {
            // compute the distance from the critical point
            double distance = norm( last - critical );

            // if I found that two calculated points are very very close I conclude that
            // they are the same point, so the sequence is periodic so we are computing a point
            // in the mandelbrot, so I stop the calculation
            if ( distance < FLT_EPSILON * FLT_EPSILON ) { // maybe also DBL_EPSILON is sufficient
                calculated = i;
                return -1;
            }

            // I don't do this step at every iteration to be more fast, I found that a very good
            // compromise is to use a multiplicative distance between each check
            if ( i == criticalStep * 2 ) {
                criticalStep *= 2;
                critical = last;
            }
        }

        //next_point( last, begin );
        last = last * last + begin;
        // double re = z.real() * z.real() - z.imag() * z.imag() + c.real();
        // double im = 2.0 * z.real() * z.imag() + c.imag();
        // z = complex<double>(re, im);
    }

    calculated = b->high;
    return -1;
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
int buddha_generator::findPoint ( complex_type& begin, double& centerDistance, unsigned int& contribute, unsigned int& calculated ) {
    int max, iterations = 0;
    unsigned int calculatedInThisIteration;
    double bestDistance = 64.0;
    complex_type tmp = begin;

    // 64 - 512
#define FINDPOINTMAX 	256

    calculated = 0;
    do {
        //seq[0].mutate( 0.25 * sqrt(dist), &buf );
        gaussianMutation( tmp, 0.25 * 0.25 * sqrt( bestDistance ) );

        max = evaluate( tmp, centerDistance, contribute, calculatedInThisIteration );
        calculated += calculatedInThisIteration;

        if ( max != -1 && centerDistance < bestDistance ) {
            bestDistance = centerDistance;
            begin = tmp;
        } else {
            tmp = begin;
        }
    } while ( bestDistance != 0.0 && ++iterations < FINDPOINTMAX );


    return max;
}


// the metropolis algorithm. I don't know very much about the teory under this optimization but I think is
// implemented quite well.. Maybe a better method for the transition probability can be found but I don't know.
void buddha_generator::metropolis ( ) {
    complex_type begin( 0.0, 0.0 );
    unsigned int calculated, selectedOrbitCount = 0, proposedOrbitCount = 0;
    int selectedOrbitMax = 0, proposedOrbitMax = 0, j;
    double radius = 40.0 / b->scale; // 100.0;

    //double add = 0.0; // 5.0 / b->scale;
    double distance;

    // search a point that has some contribute in the interested area
    selectedOrbitMax = findPoint( begin, distance, selectedOrbitCount, calculated );
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

        // calculate the new sequence
        proposedOrbitMax = evaluate( begin, distance, proposedOrbitCount, calculated );

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

        for ( int h = 0; h <= proposedOrbitMax - (int) b->low && proposedOrbitCount > 0 && h <= int(b->high - b->low); h++ ) {
            unsigned int i = h + b->low;
            drawPoint( seq[h], i < b->highr && i > b->lowr, i < b->highg && i > b->lowg, i < b->highb && i > b->lowb);
        }

        /*if ( points.size() > (1 << 23) ) {
            sort(points.begin(), points.end());
            bu->rawmutex.lock();
            for (auto i : points) bu->raw[i]++;
            bu->rawmutex.unlock();
            points.clear();
        }*/

        if ( finish ) break;
    }
}




// normal buddhabrot drawing function, no metropolis
void buddha_generator::normal ( ) {
    // normal uniform search
    complex_type begin( 0.0, 0.0 );
    unsigned int calculated, proposedOrbitCount = 0;
    unsigned int proposedOrbitMax;

    double distance = 0;
    // generate a random point
    gaussianMutation( begin, 2.0 );
    // calculate the sequence
    proposedOrbitMax = evaluate( begin, distance, proposedOrbitCount, calculated );
    computed += calculated;

    for ( int h = 0; h <= proposedOrbitMax - (int) b->low && h <= int(b->high - b->low); h++ ) {
        unsigned int i = h + b->low;
        drawPoint( seq[h], i < b->highr && i > b->lowr, i < b->highg && i > b->lowg, i < b->highb && i > b->lowb);
    }
}


void buddha_generator::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run()";

    while ( true ) {
        metropolis( );
        //normal( );

        lock_guard<mutex> locker ( execution );
        if ( finish ) break;
    }


    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run(), finished";
}
