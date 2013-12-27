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

#ifndef BUDDHA_H
#define BUDDHA_H

#include <string>
#include <vector>
#include <cmath>
#include <stdlib.h>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include "complex.h"
#include "staticStuff.h"

#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>

using namespace std;



struct isize {
    uint w;
    uint h;

    uint height ( ) { return h; }
    uint width ( ) { return w; }
};

enum current_status { PAUSE, STOP, RUN };
class buddha_generator;





class buddha {

    int threads;
    vector<buddha_generator*> generators;
    current_status generatorsStatus;

    //void preprocessImage ( );
    void createImage ( );
public:
    // for waiting that a BuddhaGenerator has been stopped
    condition_variable semaphore;

    // since this class is also used as "container" for the various generators
    // I use directly public variables instead private members and functions like set*()
    // buddhabrot characteristics
    double maxre, maxim;
    double minre, minim;
    double cre, cim;
    uint low;
    uint lowr;
    uint lowg;
    uint lowb;
    uint high;
    uint highr;
    uint highg;
    uint highb;
    double scale;

    // these can be calculated from the previous but they are useful
    double rangere, rangeim;
    uint w;
    uint h;
    uint size;


    // things for the plot
    uint* raw;          // i want to avoid this in the future XXX
    uint* RGBImage;		// here will be built the QImage
    float rmul, gmul, bmul, realContrast, realLightness;
    uint contrast, lightness;
    uint maxr, minr, maxb, minb, maxg, ming;

    static const uint maxLightness = 200;
    static const uint maxContrast = 200;
    //static const uint maxFps = 40;





    buddha ();
    ~buddha ( );

    void reduceStep ( int i, bool check );
    void reduce ( );
    void run( );

    void imageCreated( );
    void stoppedGenerators( bool);
    void startedGenerators( bool);
    void settedValues( );


    // never call directly these functions from the GUI!!!
    void startGenerators( );
    void stopGenerators( );
    void updateRGBImage( );
    void pauseGenerators( );
    void resumeGenerators( );
    void set( double cre, double cim, double scale, uint lr, uint lg, uint lb, uint hr, uint hg, uint hb, isize wsize, bool pause );
    void clearBuffers ( );
    void resizeBuffers ( );
    void resizeSequences ( );
    void changeThreadNumber( int threads );
    void saveScreenshot (string &fileName );
    void setContrast( int value );
    void setLightness( int value );

    void dump ( );
};


#endif

