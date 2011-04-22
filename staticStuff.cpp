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


//#include <unistd.h>
//#include <sys/time.h>
#include <limits.h>
#include "staticStuff.h"
#include <cmath>
/*
void getInfo ( unsigned int* raw, unsigned int size, unsigned int& minr, float& midr, unsigned int& maxr, 
		      unsigned int& ming, float& midg, unsigned int& maxg, unsigned int& minb, float& midb, unsigned int& maxb ) {
	float sumr = 0.0, sumg = 0.0, sumb = 0.0;
	
	minr = ming = minb = UINT_MAX;
	maxr = maxg = maxb = 0;
	for ( unsigned int i = 0; i < 3 * size; i += 3 ) {
		if ( raw[i + 0] > maxr ) maxr = raw[i + 0];
		if ( raw[i + 0] < minr ) minr = raw[i + 0];
		if ( raw[i + 1] > maxg ) maxg = raw[i + 1];
		if ( raw[i + 1] < ming ) ming = raw[i + 1];
		if ( raw[i + 2] > maxb ) maxb = raw[i + 2];
		if ( raw[i + 2] < minb ) minb = raw[i + 2];
		sumr += raw[i + 0];
		sumg += raw[i + 1];
		sumb += raw[i + 2];
	}
	
	midr = sumr / size;
	midg = sumg / size;
	midb = sumb / size;
	
	//qDebug() <<"r = %d %f %d\n", minr, midr, maxr );
	//qDebug() <<"g = %d %f %d\n", ming, midg, maxg );
	//qDebug() <<"b = %d %f %d\n", minb, midb, maxb );
}
*/


/*
int32_t random ( struct random_data* buf ) {
	int32_t out;
	
	random_r( buf, &out );
	return out;
}*/

