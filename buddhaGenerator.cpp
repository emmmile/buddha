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

int BuddhaGenerator::randomTest ( unsigned int& calculated ) {

	static Random gen( seed );

	for ( int i = 0; i < 1000000; i++ ) {
		seq[0].re = seq[0].im = 0.0;
		gen.exponential( seq[0].re, seq[0].im,0.0001 );

		mutex.lock();
		drawPoint( seq[0], 1, 1,1 );
		if ( !flow( ) ) {
			mutex.unlock();
			return -1;
		}
		mutex.unlock();
	}

	return 0;
}
*/





void BuddhaGenerator::initialize ( Buddha* b ) {
	qDebug() << "BuddhaGenerator::initialize()";
	this->b = b;
	
	seed = powf ( (unsigned long int) this & 0xFF, M_PI ) + ( ( (unsigned long int) this >> 16 ) & 0xFFFF );
	
	//buf.state = (int32_t*) statebuf; // this fixes the segfault
	//initstate_r( seed, statebuf, sizeof( statebuf ), &buf );
	generator.seed( seed );
	
	raw = (unsigned int*) realloc( raw, 3 * b->size * sizeof( unsigned int ) );
	memset( raw, 0, 3 * b->size * sizeof( unsigned int ) );
	seq.resize( b->high - b->low );
	
	status = RUN;
	
	qDebug() << "Initialized generator" << (void*) this << "with seed" << seed;
}

bool BuddhaGenerator::flow ( ) {
	//qDebug() <<"flow()\n" );
	// note that pauseCondition has been set previously
	//QMutexLocker lock ( &mutex );
	
	if ( status == PAUSE ) {
		//pauseCondition->wakeOne();
		b->semaphore.release( 1 );
		resumeCondition.wait( &mutex );
		//b->semaphore.acquire( 1 );
	}
	if ( status == STOP ) return false;
	
	return true;
}


void BuddhaGenerator::pause ( ) {
	//QMutexLocker lock ( &mutex );
	status = PAUSE;
	//pauseCondition->wait( &mutex );
}

void BuddhaGenerator::resume ( ) {
	//QMutexLocker lock ( &mutex );
	status = RUN;
	resumeCondition.wakeOne();
}

void BuddhaGenerator::stop ( ) {
	//QMutexLocker lock ( &mutex );
	status = STOP;
	//pauseSemaphore->release( 1 );
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
	
        // the y coordinates are referred to the point (b->minre, b->maxim), and are symetric in
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
int BuddhaGenerator::evaluate ( complex& begin, double& centerDistance,
				unsigned int& contribute, unsigned int& calculated ) {
	complex last = begin;	// holds the last calculated point
	complex critical;	// for periodicity check
	unsigned int j = 0, criticalStep = STEP;
	double tmp = 64.0;
	bool isInside;
	centerDistance = 64.0;
	contribute = 0;

	for ( unsigned int i = 0; i < b->high; ++i ) {
		// when low <= i < high the points are saved for drawing
		if ( i >= b->low ) seq[j++] = last;

		// this checks if the last point is inside the screen
		if ( ( isInside = inside( last ) ) ) {
			centerDistance = 0.0;
			++contribute;
		}

		// if we didn't passed inside the screen calculate the distance
		// it will update after the variable centerDistance
		if ( centerDistance != 0.0 ) {
			tmp = ( last.re - b->cre ) * ( last.re - b->cre ) +
			      ( last.im - b->cim ) * ( last.im - b->cim );
			if ( tmp < centerDistance && last.mod() < 4.0 ) centerDistance = tmp;
		}

		// test the stop condition and eventually continue a little bit
		if ( last.mod( ) > 4.0 ) {
			if ( !isInside ) {
				calculated = i;
				return i - 1;
			}
		}

		if ( i == criticalStep ) {
			critical = last;
		} else if ( i > criticalStep ) {
			// compute the distance from the critical point
			tmp = ( last.re - critical.re ) * ( last.re - critical.re ) +
			      ( last.im - critical.im ) * ( last.im - critical.im );

			// if I found that two calculated points are very very close I conclude that
			// they are the same point, so the sequence is periodic so we are computing a point
			// in the mandelbrot, so I stop the calculation
			if ( tmp < FLT_EPSILON * FLT_EPSILON ) { // maybe also DBL_EPSILON is sufficient
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


		tmp = last.re * last.re - last.im * last.im + begin.re;
		last.im = 2.0 * last.re * last.im + begin.im;
		last.re = tmp;
	}
	
	calculated = b->high;
	return -1;
}



inline void BuddhaGenerator::gaussianMutation ( complex& z, double radius ) {
	double redev, imdev;
	generator.gaussian( redev, imdev, radius );
	z.re += redev;
	z.im += imdev;
}

inline void BuddhaGenerator::exponentialMutation ( complex& z, double radius ) {
	double redev, imdev;
	generator.exponential( redev, imdev, radius );
	z.re += redev;
	z.im += imdev;
}


// search for a point that falls in the screen, simply moves randomly making moves
// proportional in size to the distance from the center of the screen.
// I think can be optimized a lot
int BuddhaGenerator::findPoint ( complex& begin, double& centerDistance, unsigned int& contribute, unsigned int& calculated ) {
	int max, iterations = 0;
	unsigned int calculatedInThisIteration;
	double bestDistance = 64.0;
	complex tmp = begin;


	// 64 - 512
	#define FINDPOINTMAX 	256
	
	calculated = 0;
	do {
		//seq[0].mutate( 0.25 * sqrt(dist), &buf );
		gaussianMutation( tmp, 0.25 * sqrt( bestDistance ) );
		
		
		max = evaluate( tmp, centerDistance, contribute, calculatedInThisIteration );
		calculated += calculatedInThisIteration;

		if ( max != -1 && centerDistance < bestDistance ) {
			bestDistance = centerDistance;
			begin = tmp;
		} else  tmp = begin;
	} while ( bestDistance != 0.0 && ++iterations < FINDPOINTMAX );
	
	
	return max;
}


// the metropolis algorithm. I don't know very much about the teory under this optimization but I think is
// implemented quite well.. Maybe a better method for the transition probability can be found but I don't know.
int BuddhaGenerator::metropolis ( ) {
/*<<<<<<< HEAD
	complex begin( 0.0, 0.0 );
	unsigned int calculated, total = 0, selectedOrbitCount = 0, proposedOrbitCount = 0;
	int selectedOrbitMax = 0, proposedOrbitMax = 0, j;
	double radius = 4.656612875245796924105E-10 / b->scale * 40.0; // 100.0;
=======*/

	complex begin( 0.0, 0.0 );
	unsigned int calculated, total = 0, selectedOrbitCount = 0, proposedOrbitCount = 0;
	int selectedOrbitMax = 0, proposedOrbitMax = 0, j;
	double radius = 40.0 / b->scale; // 100.0;

	//double add = 0.0; // 5.0 / b->scale;
	double distance;

	// search a point that has some contribute in the interested area
	selectedOrbitMax = findPoint( begin, distance, selectedOrbitCount, calculated );

	cout << selectedOrbitMax << endl;

	// if the search failed I exit
	if ( selectedOrbitCount == 0 ) return calculated;
	
	complex ok = begin;
	// also "how much" cicles are executed on each point is crucial. In order to have more points on the
	// screen an high iteration count could be better but, not too high because otherwise the space
	// is not sampled well. I tried values between 512 and 8192 and they works well. Over 80000 it becames strange.
	// Now i'm using something proportional on "how much the point is important".. For example how long the sequence
	// is and how many points falls on the window.
	for ( j = 0; j < max( (int) selectedOrbitCount * 256, selectedOrbitMax * 2 ); j++ ) {
		// I put the check here because of the "continue"'s in the middle that makes the thread
		// a little bit slow to respond to the changes of status
		QMutexLocker locker( &mutex );
		if ( !flow( ) ) return -1;
		locker.unlock();

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
		
		total += calculated;

		locker.relock();
		// draw the points
		for ( int h = 0; h <= proposedOrbitMax - (int) b->low && proposedOrbitCount > 0; h++ ) {
			unsigned int i = h + b->low;
			drawPoint( seq[h], i < b->highr && i > b->lowr, i < b->highg && i > b->lowg, i < b->highb && i > b->lowb);
		}
	}

	return total;
}


void BuddhaGenerator::run ( ) {
	//b->semaphore.acquire( 1 );

	int exit = 0;
	
	do {
		exit = metropolis( );

		QMutexLocker locker( &mutex );
		if ( !flow( ) ) exit = -1;
	} while ( exit != -1 );
	
	// the buddha thread maybe is waiting that I've finished
	b->semaphore.release();
}
