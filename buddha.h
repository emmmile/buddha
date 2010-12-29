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
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <cstdio>
#include "complex.h"


using namespace std;

#define ZOOM 		1
//#define FPS 		4
#define TEST		0


enum CurrentStatus { PAUSED, ABORTED, RUNNING };
enum MemoryStatus { OK, CLEAN, REALLOC };

class BuddhaGenerator;





class Buddha : public QThread {
	Q_OBJECT
	
public:	
	// for the communication with the GUI
	QMutex mutex;
	QWaitCondition stopped, restarted;
	
	// for waiting that a BuddhaGenerator has been stopped
	QWaitCondition generatorReady;
	QTimer* timer;
	
	
	bool cleaned;
	CurrentStatus status;	
	MemoryStatus memory;
	bool contrastChanged;
	
	// buddhabrot characteristics
	double maxre, maxim;
	double minre, minim;
	double cre, cim;
	unsigned int high;
	unsigned int highr;
	unsigned int highg;
	unsigned int highb;
	double scale;

	// these can be calculated from the previous but they are useful
	double rangere, rangeim;
	unsigned int w;
	unsigned int h;
	unsigned int size;
	
	// to choose between metropolis and the normal algorithm
	bool mode;
	
	
	// things for the plot
	unsigned int* raw;
	unsigned int* RGBImage;		// here will be built the QImage
	float rmul, gmul, bmul, realContrast, realLightness;
	int contrast, lightness;
	



	BuddhaGenerator* generators;
	int threads;
	float fps;
	void setFps( float );



	Buddha ( QObject *parent = 0 );
	~Buddha ( );

	void setContrast( int value );
	void setLightness( int value );
	void set( double cre, double cim, double scale, uint r, uint g, uint b, QSize wsize );
	void setStatus( CurrentStatus s );
	void setAlgorithm ( bool m );
	void clear ( );
	void realloc ( );
	void lock( ) { mutex.lock(); }
	void unlock( ) { mutex.unlock(); }
	
	
	
	void pauseGenerators( );
	void resumeGenerators( );
	bool pauseGenerator ( int i, unsigned int msec = UINT_MAX );
	void resumeGenerator ( int i );
	
	void run();
	void toImage ( );
	void preprocessImage ( );
signals:
	void doneIteration( );
public slots:
	void updateRGBImage( );
};


#endif

