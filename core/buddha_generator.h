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


#ifndef BUDDHA_GENERATOR_H
#define BUDDHA_GENERATOR_H

#include "buddha.h"
using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct buddha_generator {
    typedef complex<double> complex_type;
    //typedef simple_complex<double> complex_type;
    thread t;

    settings* b;

    // for the raw image and the sequence of points
    vector<complex_type> seq;
    vector<buddha::pixel> raw;

    unsigned long int computed;
    unsigned long int seed;
    Random generator;
    void (*next_point)(complex<double>&, complex<double>&);


    bool finish;

    // for the synchronization and for controlling the execution
    mutex execution;


    buddha_generator( );
    buddha_generator( settings* b);
    ~buddha_generator ( );

    void initialize ( settings* b );


    void gaussianMutation ( complex_type& z, double radius );
    void exponentialMutation ( complex_type& z, double radius );
    int inside ( complex_type& c );

    void drawPoint ( complex_type& c );
    int evaluate ( complex_type& begin, uint& contribute, uint& calculated );
    int evaluate ( complex_type& begin, double& distance, uint& contribute, uint& calculated );
    int evaluate_inverse ( complex_type& begin, uint& calculated );

    int findPoint ( complex_type& begin, double& centerDistance, uint& contribute, uint& calculated );
    void metropolis();
    void inverse();
	

    void start ( );
    void stop ( );

	void run ( );	
};

#endif


