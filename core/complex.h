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



#ifndef COMPLEX_H
#define COMPLEX_H

#include <iostream>
#include "staticStuff.h"

/// semplified structure to store complex numbers and generate random complex numbers
class simple_complex {
public:
	double re;
	double im;

	
    simple_complex( double re, double im ) {
		this->re = re;
		this->im = im;
	}	
	
    simple_complex( ) {
		//complex( 0.0, 0.0 );
	}
	
    inline simple_complex& operator = ( const simple_complex& z ) {
		this->re = z.re;
		this->im = z.im;
		return *this;
	}
	/*
	friend complex& operator -= ( complex& z, const complex& t ) {
		z.re -= t.re;
		z.im -= t.im;
		return z;
	}
	
	friend complex& operator += ( complex& z, const complex& t ) {
		z.re += t.re;
		z.im += t.im;
		return z;
	}*/
	
    friend const simple_complex operator - (const simple_complex& z, const simple_complex& t ) {
        return simple_complex( z.re - t.re, z.im - t.im );
	}
	
    friend const simple_complex operator + (const simple_complex& z, const simple_complex& t ) {
        return simple_complex( z.re + t.re, z.im + t.im );
	}
	
	inline double mod ( ) {
		return re * re + im * im;
	}
	
	/*inline void randomGaussian2 ( struct random_data* buf ) {
		re = -0.3333333333333333;
		im = 0.0;
		mutate ( 1.0, buf );
	}
	
	inline void random2 ( struct random_data* buf ) {
		//re = ( random( buf ) << 1 ) * 9.31322575049159384821E-10;
		//im = ( random( buf ) << 1 ) * 9.31322575049159384821E-10;
		re = scaleToTwo( random( buf ) );
		im = scaleToTwo( random( buf ) );
	}
	
	inline double randomCircle ( struct random_data* buf ) {
		while ( TRUE ) {
			re = scaleToOne( random( buf ) );
			im = scaleToOne( random( buf ) );
	
			double s = mod( );
			if ( s < 1.0 ) return s;
		}
	}
	
	void mutate ( double radius, struct random_data* buf ) {
		// XXX can be optimized I think
		// the Marsaglia polar method
		complex cc;
		double s = cc.randomCircle( buf );
		double factor = sqrt( -2.0 * log( s ) / s ) * radius;
		re += cc.re * factor;
		im += cc.im * factor;
	}*/
};


#endif
