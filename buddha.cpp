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
#include "staticutils.h"
#include "controlWindow.h"
#include <math.h>
#include <float.h>
#include <iostream>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <stdio.h>



Buddha::Buddha( QObject *parent ) : QThread( parent ) {
	status = RUNNING;
	memory = REALLOC;
	cleaned = true;
	size = w = h = highr = highg = highb = 0;
	cre = cim = scale = 0.0;
	raw = NULL;
	RGBImage = NULL;
	
	threads = QThread::idealThreadCount();
	if ( threads == -1 ) threads = 1;
	
	// initialization
	generators = new BuddhaGenerator [threads];
	
	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT( updateRGBImage()));
}

void Buddha::updateRGBImage( ) {
	ttime();
	memset( raw, 0, 3 * size * sizeof( int ) );
	
	for ( int i = 0; i < threads; ++i ) {
		generators[i].wmutex.lock();
		
		// I take the results calculated from BuddhaGenerator i, summing to the local array
		for ( unsigned int j = 0; j < 3 * size && generators[i].memory != REALLOC; ++j )
			raw[j] += generators[i].raw[j];
			
		generators[i].wmutex.unlock();
	}
	
	_print( "Image taken in " << ttime() );
	
	// I execute a preprocessing of the image to shorten the part in mutual exclusion
	preprocessImage();	
	
	mutex.lock();
	if ( status == RUNNING ) {
		toImage( );
		emit doneIteration( );
	}
	mutex.unlock();
}

void Buddha::set( double re, double im, double s, uint r, uint g, uint b, QSize wsize ) {
	
	cre = re;
	cim = im;
	scale = s;
	w = wsize.width();
	h = wsize.height();
	
	// I reallocate only if the dimensions are changed otherwise I simply clean the memory
	if ( size != w * h ) {
		size = w * h;
		memory = REALLOC;
	} else  memory = CLEAN;
	
	rangere = w / scale;
	rangeim = h / scale;
	minre = cre - rangere * 0.5;
	maxre = cre + rangere * 0.5;
	minim = cim - rangeim * 0.5;
	maxim = cim + rangeim * 0.5;
	highr = r;
	highg = g;
	highb = b;
	high = max( max( highr, highg ), highb );
	
	status = RUNNING;
}

Buddha::~Buddha ( ) {
	_print( "Buddha destructor..." );
	free( raw );
	free( RGBImage );
	
	delete[] generators;
}














void Buddha::setLightness ( int lightness ) {
	//mutex.lock();
	this->lightness = lightness;
	//mutex.unlock();
}

void Buddha::setContrast ( int contrast ) {
	//mutex.lock();
	this->contrast = contrast;
	//mutex.unlock();
}

void Buddha::setAlgorithm ( bool m ) {
	mode = m;
}

void Buddha::setStatus ( CurrentStatus s ) {
	status = s;
}

void Buddha::clear ( ) {
	//printf( "clear()\n" );
	memset( RGBImage, 0, size * sizeof( int ) );
	memset( raw, 0, 3 * size * sizeof( int ) );
	
	for ( int i = 0; i < threads; ++i ) {
		generators[i].wmutex.lock();
		generators[i].memory = CLEAN;
		generators[i].wmutex.unlock();
	}
	
	cleaned = true;
	memory = OK;
}

void Buddha::realloc ( ) {
	//printf( "realloc()\n" );
	if ( raw ) free( raw );
	if ( RGBImage ) free( RGBImage );
	
	raw = (unsigned int*) calloc( size * 3, sizeof( unsigned int ) );
	RGBImage = (unsigned int*) calloc( size, sizeof( unsigned int ) );
	
	for ( int i = 0; i < threads; ++i ) {
		generators[i].wmutex.lock();
		generators[i].memory = REALLOC;
		generators[i].wmutex.unlock();
	}
	
	cleaned = true;
	memory = OK;
}










static void getInfo ( unsigned int* raw, unsigned int size, unsigned int& minr, float& midr, unsigned int& maxr, 
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
	
	//printf( "r = %d %f %d\n", minr, midr, maxr );
	//printf( "g = %d %f %d\n", ming, midg, maxg );
	//printf( "b = %d %f %d\n", minb, midb, maxb );
}





void Buddha::preprocessImage ( ) {
	unsigned int maxr, minr, maxb, minb, maxg, ming;
	float midr, midg, midb;

	ttime( );

	cleaned = false;
	// this can be optimized but I don't think it impacts very much on the total time
	// taken to convert the data into RGB
	getInfo( raw, size, minr, midr, maxr, ming, midg, maxg, minb, midb, maxb );
	realContrast = (float) contrast / ControlWindow::maxContrast * 2.0;
	realLightness = (float) lightness / ( ControlWindow::maxLightness - lightness + 1 );
	//float contrast = 0.25;
		
	rmul = maxr > 0 ? log( scale ) / (float) powf( maxr, realContrast ) * 150.0 * realLightness : 0.0;
	gmul = maxg > 0 ? log( scale ) / (float) powf( maxg, realContrast ) * 150.0 * realLightness : 0.0;
	bmul = maxb > 0 ? log( scale ) / (float) powf( maxb, realContrast ) * 150.0 * realLightness : 0.0;
	
	_print( "Image preprocessed in " << ttime( ) );
}



void Buddha::toImage ( ) {
	//cout << "Buddha::toImage()\n";
	ttime( );
	unsigned char r, g, b;
	unsigned int j = 0;
	for ( unsigned int i = 0; i < size; ++i, j += 3 ) {
		r = min( powf( raw[j + 0], realContrast ) * rmul, 255.0f );
		g = min( powf( raw[j + 1], realContrast ) * gmul, 255.0f );
		b = min( powf( raw[j + 2], realContrast ) * bmul, 255.0f );
		
		RGBImage[i] = r << 16 | g << 8 | b;
	}
	
	
	_print( "Image created in " << ttime( ) << "\n" );
}


bool Buddha::pauseGenerator ( int i, unsigned int msec ) {
	generators[i].wmutex.lock();
	generators[i].setStatus( PAUSED );
	bool out = generatorReady.wait( &(generators[i].wmutex), msec );
	if ( !out ) generators[i].setStatus( RUNNING );
	generators[i].wmutex.unlock();
	
	return out;
}

void Buddha::resumeGenerator ( int i ) {
	generators[i].wmutex.lock();
	generators[i].setStatus( RUNNING );
	generators[i].wcondition.wakeOne();
	generators[i].wmutex.unlock();
}

void Buddha::pauseGenerators ( ) {
	for ( int i = 0; i < threads; ++i ) pauseGenerator( i );
		
	//status = PAUSED;
}

void Buddha::resumeGenerators ( ) {
	for ( int i = 0; i < threads; ++i )
		resumeGenerator( i );
		
	//status = RUNNING;
}


void Buddha::setFps ( float fps ) {
	this->fps = fps;
	int sleep = (fps == 0.0) ? 0x0FFFFFFF : 1000.0f / fps;
	
	mutex.lock();
	timer->stop();
	timer->start( sleep );
	mutex.unlock();
}


void Buddha::run ( ) {

	mutex.lock();
	realloc( );
	mutex.unlock();

	// start the generators
	for ( int i = 0; i < threads; ++i ) {
		generators[i].initialize( this );
		generators[i].start();
	}
	
	mutex.lock();
	timer->start( 1000.0f / fps );
	mutex.unlock();
	
	
	// I don't like this.. I don't really know how to synchronize all the threads,
	// at the moment this thread sleep for a while and then sees what there is to do.
	// But surely this can be done in a more efficient and elegant way. 
	while ( true ) {
		usleep( 100000.0f / fps );
		
		
		mutex.lock();
		if ( status == PAUSED ) {
			timer->stop();
			pauseGenerators();
			
			stopped.wakeOne( );
			restarted.wait( &mutex );
			
			if ( memory == REALLOC )
				realloc( );
			if ( memory == CLEAN )
				clear( );
			
			resumeGenerators();
			timer->start( 1000.0f / fps );
		}
		if ( status == ABORTED ) {
			mutex.unlock();
			timer->stop( );
			break;
		}
		
		status = RUNNING;
		mutex.unlock();
	}
	
	
	// cleaning
	for ( int i = 0; i < threads; ++i ) {
		generators[i].wmutex.lock();
		generators[i].setStatus( ABORTED );
		generators[i].wcondition.wakeOne();
		generators[i].wmutex.unlock();
	}
	
	for ( int i = 0; i < threads; ++i )
		generators[i].wait();
}

