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
#define STEP		16
#define METTHD		16000

using namespace std;

// these are not for the buddhabrot, are others functions

// this is the anti-buddhabrot evaluate function
// it would be nice to put a flag somewhere and have the possibility
// somewhere to choose from the interface if render the buddhabrot or
// the antibuddhabrot.
/*int BuddhaGenerator::anti ( unsigned int& calculated ) {
	complex z;
	unsigned int i;
	
	for ( i = 0; i < b->high - 1; ) {
		
		if ( seq[i].mod( ) > 4.0 ) {
			calculated = i;
			return -1;
		}
		
		z.re = sqr( seq[i].re ) - sqr( seq[i].im ) + seq[0].re;
		z.im = 2.0 * seq[i].re * seq[i].im + seq[0].im;
		seq[++i].re = z.re;
		seq[i].im = z.im;
	}
	
	calculated = b->high;
	return b->high - 1;
}

int BuddhaGenerator::test ( unsigned int& calculated ) {
	unsigned int i;
	static const double a = -1.5809424;
	static const double bb = -0.74525865;
	static const double c = -1.302572;
	static const double d = -0.605825;
	
	for ( i = 0; i < b->high - 1; i++ ) {
		seq[i+1].re = sin( a * seq[i].im ) - cos( bb * seq[i].re );
		seq[i+1].im = sin( c * seq[i].re ) - cos( d * seq[i].im );
	}
	
	calculated = b->high;
	return b->high;
}*/


























void BuddhaGenerator::initialize ( Buddha* b ) {
	qDebug() << "BuddhaGenerator::initialize()";
	this->b = b;
	
	seed = powf ( (unsigned long int) this & 0xFF, M_PI ) + ( ( (unsigned long int) this >> 16 ) & 0xFFFF );
	
	buf.state = (int32_t*) statebuf; // this fixes the segfault
	initstate_r( seed, statebuf, sizeof( statebuf ), &buf );
	
	raw = (unsigned int*) realloc( raw, 3 * b->size * sizeof( unsigned int ) );
	memset( raw, 0, 3 * b->size * sizeof( unsigned int ) );
	seq.resize( b->high );
	
	status = RUN;
	
	qDebug() << "Initialized generator" << (void*) this << "with seed" << seed;
}

bool BuddhaGenerator::flow ( ) {
	//qDebug() <<"flow()\n" );
	// note that pauseCondition has been set previously
	//QMutexLocker lock ( &mutex );
	
	if ( status == PAUSE ) {
		pauseCondition->wakeOne();
		resumeCondition.wait( &mutex );
	}
	if ( status == STOP ) return false;
	
	return true;
}


void BuddhaGenerator::pause ( QWaitCondition* pauseCondition ) {
	//QMutexLocker lock ( &mutex );
	status = PAUSE;
	this->pauseCondition = pauseCondition;
	pauseCondition->wait( &mutex );
}

void BuddhaGenerator::resume ( ) {
	//QMutexLocker lock ( &mutex );
	status = RUN;
	resumeCondition.wakeOne();
}

void BuddhaGenerator::stop ( QWaitCondition* pauseCondition ) {
	//QMutexLocker lock ( &mutex );
	status = STOP;
	this->pauseCondition = pauseCondition;
	pauseCondition->wait( &mutex );
}







void BuddhaGenerator::drawPoint ( complex& c, bool drawr, bool drawg, bool drawb ) {
	register unsigned int x, y;
	
	#define plotIm( c, drawr, drawg, drawb ) \
	if ( c.im > b->minim && c.im < b->maxim ) { \
		y = ( b->maxim - c.im ) * b->scale; \
		if ( drawb )	raw[ y * 3 * b->w + 3 * x + 2 ]++;	\
		if ( drawr )	raw[ y * 3 * b->w + 3 * x + 0 ]++;	\
		if ( drawg )	raw[ y * 3 * b->w + 3 * x + 1 ]++;	\
	}
	
	if ( c.re < b->minre ) return;
	if ( c.re > b->maxre ) return;
	
	x = ( c.re - b->minre ) * b->scale;
	//if ( x >= b->w ) return; // activate in case of problems
	
	// the y coordinates are referred to the point (b->minre, b->maxim), and are simmetric in
	// respect of the real axis (re = 0). So I draw always also the simmetric point (I try).
	plotIm( c, drawr, drawg, drawb );
	
	c.im = -c.im;
	plotIm( c, drawr, drawg, drawb );
}


// test if a point is inside the interested area
int BuddhaGenerator::inside ( complex& c ) {
	return  c.re <= b->maxre && c.re >= b->minre && 
		( ( c.im <= b->maxim && c.im >= b->minim ) || 
		( -c.im <= b->maxim && -c.im >= b->minim ) );
		
	//return  c.re <= b->maxre && c.re >= b->minre && c.im <= b->maxim && c.im >= b->minim ;
}



// this is the main function. Here little modifications impacts a lot on the speed of the program!
int BuddhaGenerator::evaluate ( unsigned int& calculated ) {
	complex z;
	unsigned int i, critical = STEP;
	double dist;
	
	
	for ( i = 0; i < b->high - 1; ) {
		if ( seq[i].mod( ) > 4.0 ) {
			// slower but more beautiful to see. I stop only if I'm outside of the screen.
			if ( !inside( seq[i] ) ) {
				calculated = i;
				return i - 1;
			}
		}
		
		z.re = sqr( seq[i].re ) - sqr( seq[i].im ) + seq[0].re;
		z.im = 2.0 * seq[i].re * seq[i].im + seq[0].im;
		seq[++i].re = z.re;
		seq[i].im = z.im;
		
		
		if ( i > critical ) {
			// compute the distance
			z = z - seq[critical];
			dist = z.mod( );
			
			// if I found that two calculated points are very very close I conclude that
			// they are the same point, so the sequence is periodic so we are computing a point
			// in the mandelbrot, so I stop the calculation
			if ( dist < FLT_EPSILON * FLT_EPSILON ) {
				calculated = i;
				return -1;
			}
			
			// I don't do this step at every iteration to be more fast, I found that a very good
			// compromise is to use a multiplicative distance between each check
			if ( i == critical * 2 ) critical *= 2;
		}
	}
	
	calculated = b->high;
	return -1;
}


// compute the minimum distance from the center of the plot and the points in 
// the calculated sequence. If a point falls inside the screen the distance is set to 0
double BuddhaGenerator::distance ( unsigned int slen ) {
	double min = 128.0;
	double dist;
	complex tmp;
	
	for ( unsigned int i = 0; i <= slen; i++ ) {
		if ( inside( seq[i] ) )
			return 0.0;
		else {
			tmp.re = seq[i].re - b->cre;
			tmp.im = seq[i].im - b->cim;
			dist = tmp.mod( );
			
			if ( dist < min ) min = dist;
		}
	}
	
	return min;
}


// search for a point that falls in the screen, simply moves randomly making moves
// proportional in size to the distance from the center of the screen.
// I think can be optimized a lot
int BuddhaGenerator::findPoint ( unsigned int& calculated ) {
	int max, iterations = 0;
	double dist = 64.0, newDist;
	unsigned int calculatedd;
	complex ok ( 0.0, 0.0 );
	
	// 64 - 512
	#define FINDPOINTMAX 	256
	
	calculated = 0;
	do {
		seq[0] = ok;
		seq[0].mutate( 0.25 * sqrt(dist), &buf );
		
		
		max = evaluate( calculatedd );
		calculated += calculatedd;	
		if ( max != -1 && ( newDist = distance( max ) ) < dist ) {
			dist = newDist;
			ok = seq[0];
		}
	} while ( dist != 0.0 && ++iterations < FINDPOINTMAX );
	
	
	return max;
}



// how many points of the sequence fall on the screen?
unsigned int BuddhaGenerator::contribute ( int maxIndex ) {
        unsigned int count = 0;
	
	for ( int j = 0; (int) j <= maxIndex; j++ )
		count += inside( seq[j] );
	
	return count;
}


// normal buddhabrot drawing function, no metropolis
int BuddhaGenerator::normal ( ) {
	// normal uniform search
	int maxIndex;
	unsigned int calculated = 0;
	
	// generate a random point
	seq[0].randomGaussian2( &buf );
	// calculate the sequence
	maxIndex = evaluate( calculated );

	// draw on the plot, synchronization stuff
	QMutexLocker locker( &mutex );
	for ( unsigned int j = 0; (int) j <= maxIndex && j < b->high; j++ )
		drawPoint( seq[j], j < b->highr, j < b->highg, j < b->highb );
	
	if ( !flow( ) ) 
		return -1;
	else 	return calculated;
}


// the metropolis algorithm. I don't know very much about the teory under this optimization but I think is
// implemented quite well.. Maybe a better method for the transition probability can be found but I don't know.
int BuddhaGenerator::metropolis ( ) {

	unsigned int calculated, total = 0;
	int selectedOrbitCount = 0, proposedOrbitCount = 0, selectedOrbitMax = 0, proposedOrbitMax = 0, j;
	double radius = 4.656612875245796924105E-10 / b->scale * 40.0; // 100.0;
	//double add = 0.0; // 5.0 / b->scale;
	
	// search a point that has some contribute in the interested area
	selectedOrbitMax = findPoint( calculated );
	selectedOrbitCount = contribute( selectedOrbitMax );
	
	// if the search failed I exit
	if ( selectedOrbitCount == 0 ) 
		return calculated;
	
	complex ok ( seq[0] );
	// also "how much" cicles are executed on each point is crucial. In order to have more points on the
	// screen an high iteration count could be better but, not too high because otherwise the space
	// is not sampled well. I tried values between 512 and 8192 and they works well. Over 80000 it becames strange.
	// Now i'm using something proportional on "how much the point is important".. For example how long the sequence
	// is and how many points falls on the window.
	for ( j = 0; j < max( selectedOrbitCount * 256, selectedOrbitMax * 2 ); j++ ) {
		// I put the check here because of the "continue"'s in the middle that makes the thread
		// a little bit slow to respond to the changes of status
		mutex.lock();
		if ( !flow( ) ) {
			mutex.unlock();
			return -1;
		}
		mutex.unlock();

		seq[0] = ok;
		// the radius of the mutations influences a lot the quality of the rendering AND the speed.
		// I think that choose a random radius is the best way otherwise I noticed some geometric artifacts
		// around the point (-1.8, 0) for example. This artifacts however depend also on the number of iterations
		// explained above.
		seq[0].mutate( random( &buf ) * radius /* + add */, &buf );
		
		// calculate the new sequence
		proposedOrbitMax = evaluate( calculated );
		
		// the sequence is periodic, I try another mutation
		if ( proposedOrbitMax <= 0 ) continue;
		
		// maybe the sequence is not periodic but It doesn't contribute on the actual region
		proposedOrbitCount = contribute( proposedOrbitMax );
		if ( proposedOrbitCount == 0 ) continue;
		
		
		// calculus of the transitional probability. One point is more probable of being
		// chose if generates a lot of points in the window
		double alpha =  proposedOrbitMax * proposedOrbitMax * proposedOrbitCount /
				double( selectedOrbitMax * selectedOrbitMax * selectedOrbitCount );
		
		if ( alpha > scaleToOnePositive( random( &buf ) ) ) {
			ok = seq[0];
			selectedOrbitCount = proposedOrbitCount;
			selectedOrbitMax = proposedOrbitMax;
		}
		
		total += calculated;

		// draw the points
		QMutexLocker locker( &mutex );
		for ( unsigned int h = 0; (int) h <= proposedOrbitMax && h < b->high && proposedOrbitCount > 0; h++ )
			drawPoint( seq[h], h < b->highr, h < b->highg, h < b->highb );
	}
	
	return total;
}


void BuddhaGenerator::run ( ) {
	int exit = 0;
	
	do {
		exit = metropolis( );
	} while ( exit != -1 );
	
	// another thread maybe is waiting that I've finished
	if ( pauseCondition )
		pauseCondition->wakeOne();
}



