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


#ifndef UTILS_H
#define UTILS_H

#include <cstdlib>

#define sqr( x )		({ typeof(x) __x = (x); __x * __x; })

#define XSTR(s) STR(s)
#define STR(s) #s


#if TEST > 0
# undef _print
# define _print( str )		std::cout << str << endl;
#else
# undef _print
# define _print( str )		;
#endif


typedef unsigned char uchar;
typedef unsigned int uint;


struct isize {
    uint w;
    uint h;

    uint height ( ) { return h; }
    uint width ( ) { return w; }
};


// x MUST be an int32_t
/*#define scaleToOne(x)		( ( (x) << 1 ) / (double) RAND_MAX )
#define scaleToTwo(x)		( scaleToOne(x) * 2.0 )
#define scaleToOnePositive(x)	( (x) / (double) RAND_MAX )*/




//void getInfo ( unsigned int* raw, unsigned int size, unsigned int& minr, float& midr, unsigned int& maxr,
//		      unsigned int& ming, float& midg, unsigned int& maxg, unsigned int& minb, float& midb, unsigned int& maxb );


#endif
