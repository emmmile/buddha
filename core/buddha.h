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

#include <atomic>
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

#include "atomic_wrapper.h"
#include "mandelbrot.h"
#include "settings.h"

#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>

using namespace std;


struct buddha_generator;

class buddha {


    vector<buddha_generator*> generators;

public:
    typedef uint32_t pixel;
    typedef complex<double> complex_type;
    typedef vector<atomic_wrapper<pixel>> vector_type;
    typedef mt19937_64 random_engine;
    //typedef std::atomic_uint_fast32_t pixel;  



    mandelbrot<complex_type> core;

    settings s;

    vector_type raw;

   
    unsigned long long int computed;
    double totaltime;



    buddha ( const settings& s );
    ~buddha ( );

    void clearBuffers ( );

    void reduce ( );
    void save ( );
    void load ( );

    void startGenerators( );
    void stopGenerators( );

    void run( );
};


#endif

