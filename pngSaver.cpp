
#include "pngSaver.h"
#include <cstdio>


void PNGSaver::set ( Buddha* b, QString fileName ) {
	this->b = b;
	this->fileName = fileName;
}

void PNGSaver::run ( ) {
	b->lock();
	//printf( "%p %d %d\n", b->RGBImage, b->w, b->h );
	QImage out( (uchar*) b->RGBImage, b->w, b->h, QImage::Format_RGB32 );
	out.save( fileName, "PNG" );
	b->unlock();
}
