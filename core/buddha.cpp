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
#include "controlWindow.h"
#include <math.h>
#include <float.h>
#include <iostream>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <stdio.h>




Buddha::Buddha( QObject *parent ) : QThread( parent ) {
    size = w = h = lowr = lowg = lowb = highr = highg = highb = 0;
    cre = cim = scale = 0.0;
    raw = NULL;
    RGBImage = NULL;
    threads = 0;
    generatorsStatus = STOP;


#if QTOPENCL
    if ( !context.create( QCLDevice::GPU ) )
        qFatal("Could not create OpenCL context");

    program = context.buildProgramFromSourceFile( "./convertImage.cl" );
    convert = program.createKernel("convertImage");
#endif

    start();
    // this is foundamental for a correct handling of the signals.
    // read here for explainations: http://huntharo.com/huntharo/Blog/Entries/2009/8/21_QThread_signals_slots_-_Why_your_calls_stay_in_the_main_thread.html
    QObject::moveToThread( this );
}





void Buddha::setLightness ( int lightness ) {
    //mutex.lock();
    this->lightness = lightness;
    realLightness = (float) lightness / ( maxLightness - lightness + 1 );
    //mutex.unlock();
}

void Buddha::setContrast ( int contrast ) {
    //mutex.lock();
    this->contrast = contrast;
    realContrast = (float) contrast / maxContrast * 2.0;
    //mutex.unlock();
}


/*void Buddha::preprocessImage ( ) {
    //unsigned int maxr, minr, maxb, minb, maxg, ming;
    float midr, midg, midb;

    // this can be optimized but I don't think it impacts very much on the total time
    // taken to convert the data into RGB
    getInfo( raw, size, minr, midr, maxr, ming, midg, maxg, minb, midb, maxb );
    //realContrast = (float) contrast / ControlWindow::maxContrast * 2.0;
    //realLightness = (float) lightness / ( ControlWindow::maxLightness - lightness + 1 );
    //float contrast = 0.25;

    rmul = maxr > 0 ? log( scale ) / (float) powf( maxr, realContrast ) * 150.0 * realLightness : 0.0;
    gmul = maxg > 0 ? log( scale ) / (float) powf( maxg, realContrast ) * 150.0 * realLightness : 0.0;
    bmul = maxb > 0 ? log( scale ) / (float) powf( maxb, realContrast ) * 150.0 * realLightness : 0.0;
}*/



void Buddha::createImage ( ) {
    //cout << "Buddha::toImage()\n";
    unsigned char r, g, b;
    unsigned int j = 0;
    for ( unsigned int i = 0; i < size; ++i, j += 3 ) {
        r = min( powf( raw[j + 0], realContrast ) * rmul, 255.0f );
        g = min( powf( raw[j + 1], realContrast ) * gmul, 255.0f );
        b = min( powf( raw[j + 2], realContrast ) * bmul, 255.0f );

        RGBImage[i] = r << 16 | g << 8 | b;
    }
}


// this function takes the raw data from generator i and sums it
// to the local raw array. The main difference is that if QTOPENCL is activated
// I have to use an ARGB array of ints instead the simple RGB array kept by
// every generator. This is because i get a lot of strange errors from the
// execution of the opencl kernel. I don't know if this is a bug but for the
// moment i keep this (useless) difference between using ARGB and RGB.
void Buddha::reduceStep ( int i, bool checkValues ) {
    unsigned int j;
    if ( checkValues ) maxr = maxg = maxb = 0;

    QMutexLocker( &generators[i]->mutex );
#if QTOPENCL
    unsigned int k = 0;
    for ( j = 0; j < 3 * size; j += 3, k += 4 ) {
        raw[k+1] += generators[i]->raw[j+0];
        raw[k+2] += generators[i]->raw[j+1];
        raw[k+3] += generators[i]->raw[j+2];

        if ( checkValues ) {
            if ( raw[k+1] > maxr ) maxr = raw[k+1];
            if ( raw[k+2] > maxg ) maxg = raw[k+2];
            if ( raw[k+3] > maxb ) maxb = raw[k+3];
        }
    }

#else
    for ( j = 0; j < 3 * size; j += 3 ) {
        raw[j+0] += generators[i]->raw[j+0];
        raw[j+1] += generators[i]->raw[j+1];
        raw[j+2] += generators[i]->raw[j+2];

        if ( checkValues ) {
            if ( raw[j+0] > maxr ) maxr = raw[j+0];
            if ( raw[j+1] > maxg ) maxg = raw[j+1];
            if ( raw[j+2] > maxb ) maxb = raw[j+2];
        }
    }
#endif	

    if ( checkValues ) {
        rmul = maxr > 0 ? log( scale ) / (float) powf( maxr, realContrast ) * 150.0 * realLightness : 0.0;
        gmul = maxg > 0 ? log( scale ) / (float) powf( maxg, realContrast ) * 150.0 * realLightness : 0.0;
        bmul = maxb > 0 ? log( scale ) / (float) powf( maxb, realContrast ) * 150.0 * realLightness : 0.0;
    }
}


// Performs the whole reduce part. In the last step I also compute the
// max calculation on the raw array.
// TODO. This could be done in logarithmic time using cooperation between
// generators, for the moment I keep this version for simplicity.
void Buddha::reduce ( ) {
#if QTOPENCL
    memset( raw, 0, 4 * size * sizeof( int ) );
#else
    memset( raw, 0, 3 * size * sizeof( int ) );
#endif
    for ( int i = 0; i < threads; ++i )
        reduceStep( i, i == (threads - 1) );
}


void Buddha::updateRGBImage( ) {
    QTime time;
    time.start();

    reduce();
    int elapsed = time.elapsed();
    time.start();
#if QTOPENCL
    srcImageBuffer = context.createImage2DHost(
                QCLImageFormat( QCLImageFormat::Order_ARGB, QCLImageFormat::Type_Unnormalized_UInt32 ),
                raw, QSize( w, h ), QCLMemoryObject::ReadOnly );

    /*BOOST_LOG_TRIVIAL(debug) << "bestLocalWorkSize()" << convert.bestLocalWorkSizeImage2D() << "\n"
         << "globalWorkSize()" << convert.globalWorkSize() << "\n"
         << "localWorkSize()" << convert.localWorkSize() << "\n"
         << "preferredWorkSizeMultiple()" << convert.preferredWorkSizeMultiple();*/

    convert( srcImageBuffer, dstImageBuffer, realContrast, rmul, gmul, bmul );

    mutex.lock();
    dstImageBuffer.read(RGBImage, QRect(0, 0, w, h) );
    emit imageCreated( );
    mutex.unlock();
#else
    mutex.lock();
    createImage( );
    emit imageCreated( );
    mutex.unlock();
#endif

    BOOST_LOG_TRIVIAL(info) << "Buddha::updateRGBImage(), reduce: " << elapsed << " ms, image build: " << time.elapsed() << " ms";
    //printf( "Image build: %d ms.\n", time.elapsed() );
}

void Buddha::saveScreenshot ( QString fileName ) {
    QImage out( (uchar*) RGBImage, w, h, QImage::Format_RGB32 );
    out.save( fileName, "PNG" );

    QByteArray compress = qCompress( (const uchar*) RGBImage, w * h * sizeof(int), 9 );
    //cout << "Compressed size vs Full: " << compress.size() << " " << w * h * sizeof(int) << endl;
}

void Buddha::set( double re, double im, double s, uint lr, uint lg, uint lb, uint hr, uint hg, uint hb, QSize wsize, bool pause ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::set()";
    bool haveToClear = (wsize.width() != (int) w) || (wsize.height() != (int) h) ||(re != cre) || (im != cim) || (s != scale);

    if ( pause ) pauseGenerators( );

    w = wsize.width();
    h = wsize.height();
    // I reallocate only if the dimensions are changed otherwise I simply clean the memory
    if ( size != w * h ) {
        size = w * h;
        resizeBuffers( );
    }

    cre = re;
    cim = im;
    scale = s;
    rangere = w / scale;
    rangeim = h / scale;
    minre = cre - rangere * 0.5;
    maxre = cre + rangere * 0.5;
    minim = cim - rangeim * 0.5;
    maxim = cim + rangeim * 0.5;
    lowr = lr;
    lowg = lg;
    lowb = lb;
    highr = hr;
    highg = hg;
    highb = hb;
    high = max( max( highr, highg ), highb );
    low = min( min(lowr, lowg), lowb);
    resizeSequences( );
    //status = RUN;

    if ( pause ) {
        if ( haveToClear ) clearBuffers( );
        resumeGenerators( );
    }

    emit settedValues( );
}

Buddha::~Buddha ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::~Buddha()";
    free( raw );
    free( RGBImage );
}













void Buddha::changeThreadNumber ( int threads ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::changeThreadNumber(" << threads << "), was " << this->threads;

    // resize the array only if it is bigger
    if ( threads > (int) generators.size() ) generators.resize( threads );

    // first case: the current number of threads is less than the new one, so I have to create someting new
    for ( int i = this->threads; i < threads; ++i ) {
        // in every case if some slots in the array are empty I fill them
        if ( !generators[i] ) generators[i] = new BuddhaGenerator;
        // if we're running or in pause and I've created a new generator I have still to initialize it
        if ( generatorsStatus != STOP ) generators[i]->initialize( this );
        // if we're running I start the new generator
        if ( generatorsStatus == RUN ) generators[i]->start( );
    }

    // second case: I have to stop someting
    for ( int i = threads; i < this->threads; ++i ) {
        if ( generatorsStatus != STOP ) {
            QMutexLocker locker( &generators[i]->mutex );
            generators[i]->stop( );
        }
    }


    this->threads = threads;
}








void Buddha::resizeSequences( ) {
    for ( int i = 0; i < threads; ++i ) {
        int size = (int) high - (int) low;
        if ( size >= 0 && (int) generators[i]->seq.size() != size )
            generators[i]->seq.resize( size );
    }
}

void Buddha::resizeBuffers( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::resizeBuffers()";
    mutex.lock();
#if QTOPENCL
    raw = (unsigned int*) realloc( raw, size * 4 * sizeof( unsigned int ) );
#else
    raw = (unsigned int*) realloc( raw, size * 3 * sizeof( unsigned int ) );
#endif
    RGBImage = (unsigned int*) realloc( RGBImage, size * sizeof( unsigned int ) );
    mutex.unlock();

#if QTOPENCL
    convert.setRoundedGlobalWorkSize( QSize( w, h ) );
    //convert.setGlobalWorkSize( QSize( w, h ) );
    convert.setLocalWorkSize( convert.bestLocalWorkSizeImage2D() );
    dstImageBuffer = context.createImage2DDevice( QImage::Format_RGB32, QSize( w, h ), QCLMemoryObject::WriteOnly );
#endif


    for ( int i = 0; i < threads; ++i ) {
        QMutexLocker( &generators[i]->mutex );
        // could be done also indirectly but it not so costly
        generators[i]->raw = (unsigned int*) realloc( generators[i]->raw, 3 * size * sizeof( unsigned int ) );
    }
}

void Buddha::clearBuffers ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::clearBuffers()";
    mutex.lock();
    memset( RGBImage, 0, size * sizeof( int ) );
    memset( raw, 0, 3 * size * sizeof( int ) );
    mutex.unlock();

    for ( int i = 0; i < threads; ++i ) {
        QMutexLocker( &generators[i]->mutex );
        // could be done also indirectly but it not so costly
        if ( generators[i]->raw ) memset( generators[i]->raw, 0, 3 * size * sizeof( int ) );
    }
}

void Buddha::startGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::startGenerators()";
    for ( int i = 0; i < threads; ++i ) {
        generators[i]->initialize( this );
        generators[i]->start( );
    }

    //semaphore.acquire( threads );
    emit startedGenerators( true );
    generatorsStatus = RUN;
}


// stop the generators if they're running and if their status is different from STOP.
// XXX this can cause problems if a generator is in PAUSE, but for how the program is designed
// I think this is impossible.
// If the threads were running acquire completely the semaphore.
void Buddha::stopGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::stopGenerators()";

    for ( int i = 0; i < threads; ++i ) {
        if ( generators[i]->isRunning() ) {
            QMutexLocker locker( &generators[i]->mutex );
            if ( generators[i]->status != STOP )
                generators[i]->stop( );
        }
    }

    if ( generatorsStatus == RUN )
        semaphore.acquire( threads );

    emit stoppedGenerators( true );
    generatorsStatus = STOP;
}

// similar to the previous
void Buddha::pauseGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::pauseGenerators()";

    for ( int i = 0; i < threads && generatorsStatus == RUN; ++i ) {
        if ( generators[i]->isRunning() ) {
            QMutexLocker locker( &generators[i]->mutex );
            if ( generators[i]->status == RUN )
                generators[i]->pause( );
        }
    }

    if ( generatorsStatus == RUN ) {
        semaphore.acquire( threads );
        generatorsStatus = PAUSE;
    }
}

void Buddha::resumeGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::resumeGenerators()";

    // Here I should first release and then re-aquire incrementally the semaphore
    // from the BuddhaGenerators. I simply leave it acquired.

    for ( int i = 0; i < threads && generatorsStatus == PAUSE; ++i ) {
        QMutexLocker locker( &generators[i]->mutex );
        generators[i]->resume( );
    }

    if ( generatorsStatus == PAUSE )
        generatorsStatus = RUN;
}



void Buddha::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "Buddha::run()";
    exec( );
    // the goal is also to make things completely asychronous in respect to the interface.
    // for example waiting for the generators to stop cannot happen in the interface because this
    // blocks. Also the creation of a frame from the raw data is a costly operation that has
    // to be done separately.
    // So there is this thread that has only an event loop that processes the requests of the interface
    // (like the two mentioned above).

    stopGenerators( );
}

