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


#define TEST		0

#include <vector>
#include <QThread>
#include <QMutex>
#include <QSemaphore>
#include <QImage>
#include "complex.h"
#include "staticStuff.h"



#define DEMO_WINDOW	1

#ifdef _WIN32
#define QTOPENCL	0
#else
#define QTOPENCL	0
#endif


#if QTOPENCL
#include "qclcontext.h"
#endif

using namespace std;


static const uint maxLightness = 200;
static const uint maxContrast = 200;
static const uint maxFps = 40;

class BuddhaGenerator;
enum CurrentStatus { PAUSE, STOP, RUN };



class Buddha : public QThread {
	Q_OBJECT
	
	
#if QTOPENCL
	QCLContext context;
	QCLProgram program;
	QCLKernel convert;
	QCLImage2D srcImageBuffer;
	QCLImage2D dstImageBuffer;
#endif

	int threads;
	vector<BuddhaGenerator*> generators;
	CurrentStatus generatorsStatus;
	
	//void preprocessImage ( );
	void createImage ( );
public:	
	// for the communication with the GUI XXX maibe it can be removed
	QMutex mutex;
	
	// for waiting that a BuddhaGenerator has been stopped
	QSemaphore semaphore;
	
	// since this class is also used as "container" for the various generators
	// I use directly public variables instead private members and functions like set*()
	// buddhabrot characteristics
	double maxre, maxim;
	double minre, minim;
	double cre, cim;
        unsigned int low;
        unsigned int lowr;
        unsigned int lowg;
        unsigned int lowb;
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
	
	
	// things for the plot
	unsigned int* raw;		// i want to avoid this in the future XXX
	unsigned int* RGBImage;		// here will be built the QImage
	float rmul, gmul, bmul, realContrast, realLightness;
	int contrast, lightness;
	unsigned int maxr, minr, maxb, minb, maxg, ming;
	


#if DEMO_WINDOW
	unsigned int* demoraw;
	unsigned int* demoImage;
	double democre, democim, demoscale;
	unsigned int demow, demoh;

	double demominre, demominim;
	double demomaxre, demomaxim;
#endif

	



	Buddha ( QObject *parent = 0 );
	~Buddha ( );

	void reduceStep ( int i, bool check );
	void reduce ( );
	void run( );
signals:
	void imageCreated( );
	void stoppedGenerators( bool);
	void startedGenerators( bool);
	void settedValues( );
#if DEMO_WINDOW
	void demoSettedValues( );
#endif
public slots:
	// never call directly these functions from the GUI!!!
	void startGenerators( );
	void stopGenerators( );
	void updateRGBImage( );
	void pauseGenerators( );
	void resumeGenerators( );
        void set( double cre, double cim, double scale, uint lr, uint lg, uint lb, uint hr, uint hg, uint hb, QSize wsize, bool pause );
	void clearBuffers ( );
	void resizeBuffers ( );
	void resizeSequences ( );
	void changeThreadNumber( int threads );
	void saveScreenshot ( QString fileName );
	void setContrast( int value );
	void setLightness( int value );

#if DEMO_WINDOW
	void createDemoImage( );
	void setDemo( double cre, double cim, double scale, QSize ws, bool pause );
#endif
};


#endif

