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

#include <cfloat>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <complex>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "random.h"
#include "settings.h"

#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>

using namespace std;

typedef unsigned char uchar;
typedef unsigned int uint;

struct buddha_generator;

class buddha {


    vector<buddha_generator*> generators;

    void createImage ( );
public:
    typedef uint32_t pixel;



    settings s;

    // things for the plot
    vector<pixel> raw;
    vector<uchar> rchannel;
    vector<uchar> gchannel;
    vector<uchar> bchannel;
    uint maxr, minr, maxb, minb, maxg, ming;
    float rmul, gmul, bmul;
   
    uint computed;
    double totaltime;



    buddha ();
    ~buddha ( );

    void clearBuffers ( );

    void reduceStep ( int i, bool check );
    void reduce ( );
    void toRGB ( );
    void save ( );
    void load ( );

    void startGenerators( );
    void stopGenerators( );

    void run( const settings& s );
};


#endif

