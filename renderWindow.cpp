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


#include <QPainter>
#include <QtGui>
#include <iostream>
#include "renderWindow.h"
#include "controlWindow.h"



using namespace std;

void RenderWindow::closeEvent ( QCloseEvent* ) {
	parent->renderWinClosed( );
}



RenderWindow::RenderWindow ( ControlWindow* parent, Buddha* b ) {
	this->parent = parent;
	this->b = b;
	timer = new QTimer( this );
	mousex = mousey = 0.0;
	alreadySent = false;
	resizeSent = false;
	disabledDrawing = false;

	#ifndef QT_NO_CURSOR
	setCursor(Qt::CrossCursor);
	#endif
	setMouseTracking( true );
	resize( defaultWidth, defaultHeight );
	setWindowIcon( parent->windowIcon() );
	
	connect( timer, SIGNAL( timeout() ), this, SLOT( sendFrameRequest() ) );
	connect( this, SIGNAL( frameRequest( ) ), b, SLOT( updateRGBImage( ) ) );
	connect( b, SIGNAL( imageCreated() ), this, SLOT( receivedFrame( ) ) );
	connect( b, SIGNAL( settedValues( ) ), this, SLOT( canRestartDrawing( ) ) );

	selectionBorder = QColor( 255, 255, 255, 191 );
	selection = QColor( 255, 255, 255, 63 );
}



void RenderWindow::sendFrameRequest ( ) {
	// this only tests if we already sent a frame request, otherwise the request
	// accumulates on the buddha thread side, and slows a lot the responsivity also of the GUI
	if ( !alreadySent ) {
		emit frameRequest( );
		alreadySent = true;
	}
}


void RenderWindow::receivedFrame( ) {	
	// a frame has been calculated by the buddha thread, simply I force a repaint
	alreadySent = false;
	update( );
}

// XXX maybe change the name... this can be received also after a resize
void RenderWindow::canRestartDrawing( ) {
	disabledDrawing = false;

	// this is a strategy to don't resize the workers at every single resize event
	// i enable the sending of "resize requests" only if I processed the previous one
	resizeSent = false;
	// so, from a resizeEvent to another, there can be a lot of them that has been not
	// considered, so when I received a "resize done" from the buddha thread a manually
	// control if the sizes are equal, and if not I send a new request
	if ( size().height() != (int) b->h || size().width() != (int) b->w )
		this->resizeEvent( new QResizeEvent( size(), QSize(b->w, b->h ) ) );
}


void RenderWindow::paintEvent( QPaintEvent* ) {
	//qDebug() << "RenderWindow::paintEvent(), thread " << QThread::currentThreadId();
	QPoint off( imageOffset.x() < 0 ? 0 : imageOffset.x(), imageOffset.y() < 0 ? 0 : imageOffset.y() );

	QPainter painter ( this );
	painter.fillRect( rect(), Qt::black );
	
	if ( !disabledDrawing ) {
		// this blocks the interface if it found the buddha thread working. It's not a problem to remove them
		// but sometimes it visualize an incomplete image (if the buddha thread is already working on the next frame
		//b->mutex.lock();	// XXX these should be unnecessary
		out = QImage( (uchar*) b->RGBImage, b->w, b->h, QImage::Format_RGB32 );
		painter.drawImage( off, out, rect() & rect().translated( -imageOffset ) );
		//b->mutex.unlock();
	}
	
	painter.setPen( selectionBorder );
	if ( begMouse != endMouse ) {
		painter.setBrush( selection );
		painter.drawRect( QRect( begMouse, endMouse ) );
	}
	
	painter.drawText( 2, size().height() - 2, "Re: " + QString::number( mousex, 'f', 8 ) + 
			  ", Im: " + QString::number( mousey, 'f', 8 ) );
}



void RenderWindow::mousePressEvent(QMouseEvent *event) {
	qDebug() << "RenderWindow::mousePressEvent()";
	begMouse = endMouse = event->pos();
}


void RenderWindow::mouseMoveEvent(QMouseEvent *event) {
	//qDebug() << "RenderWindow::mouseMoveEvent()";
	mousex = ( event->x() - 0.5 * width() ) / b->scale + b->cre;
	mousey = ( -event->y() + 0.5 * height() ) / b->scale + b->cim;
	
	if (event->buttons() & Qt::LeftButton) {
		endMouse = event->pos();
		if ( endMouse.x() > size().width() )  endMouse.setX( size().width() );
        	if ( endMouse.y() > size().height() ) endMouse.setY( size().height() );
	}
	
	if ( event->buttons() & Qt::RightButton || event->buttons() & Qt::MidButton ) {
		imageOffset = event->pos() - begMouse;
	}
	
	
	update();
}


void RenderWindow::mouseReleaseEvent(QMouseEvent *event) {
	qDebug() << "RenderWindow::mouseReleaseEvent()";
	endMouse = event->pos();
	if ( endMouse == begMouse ) return;
	
	
	if ( event->button() == Qt::LeftButton ) {
	
		if ( endMouse.x() > size().width() ) endMouse.setX( size().width() );
      		if ( endMouse.y() > size().height() ) endMouse.setY( size().height() );
		
		// calcolo il nuovo zoom
		double scalex = (double) width() / fabs( ( endMouse - begMouse ).x() );
		double scaley = (double) height() / fabs( ( endMouse - begMouse ).y() );
		double scale = min( scalex, scaley );
		
		int dx = 0.5 * ( endMouse.x() + begMouse.x() - width() );
		int dy = -0.5 * ( height() - endMouse.y() - begMouse.y() );
		scroll( dx, dy );
		zoom( scale );
		// TODO ugly, this has been just set in the previous call
		parent->putValues( b->cre + dx / b->scale, b->cim - dy / b->scale, parent->getScale() );
		parent->sendValues( true );
		disabledDrawing = true;
	}
	if ( event->button() == Qt::RightButton || event->button() == Qt::MidButton ) {
		imageOffset = endMouse - begMouse;		
		
		scroll( -imageOffset.x(), -imageOffset.y() );
		parent->sendValues( true );
		disabledDrawing = true;
	}
	
	begMouse = endMouse = event->pos();
}


void RenderWindow::resizeEvent( QResizeEvent* resize ) {
	//qDebug() << "RenderWindow::resizeEvent()";
	//qDebug() << "New size: " << resize->size() << ", old size: " << resize->oldSize();
	if ( resize->oldSize() != QSize(-1,-1) && !resizeSent ) {
		parent->sendValues( true );
		disabledDrawing = true;
		resizeSent = true;
	}
}


void RenderWindow::keyPressEvent( QKeyEvent *event ) {
	switch (event->key()) {
		case Qt::Key_Plus:
			zoom( zoomFactor );
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
			break;
		case Qt::Key_Minus:
			zoom( 1.0 / zoomFactor );
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
			break;
		case Qt::Key_Left:
			scroll(-scrollStep, 0);
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
			break;
		case Qt::Key_Right:
			scroll(+scrollStep, 0);
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
			break;
		case Qt::Key_Down:
			scroll(0, +scrollStep);
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
			break;
		case Qt::Key_Up:
			scroll(0, -scrollStep);
			parent->sendValues( true );
			if ( parent->valuesChanged() ) disabledDrawing = true;
                        break;
		default:
			QWidget::keyPressEvent(event);
	}
}



void RenderWindow::wheelEvent(QWheelEvent *event) {
	double factor = event->delta() > 0 ? zoomFactor : 1.0 / zoomFactor;
	int dx, dy;
	
	if ( !zoomMode ) {
		dx = event->x() - width() * 0.5;
		dy = event->y() - height() * 0.5;
	} else	dx = dy = 0;
	
	zoom( factor, dx, dy );
	parent->sendValues( true );
	if ( parent->valuesChanged() ) disabledDrawing = true;
}


void RenderWindow::scroll ( int dx, int dy ) {
	qDebug() <<"RenderWindow::scroll()";
	if ( dx == 0 && dy == 0 ) return;

	imageOffset = QPoint( 0, 0 );
	parent->putValues( b->cre + dx / b->scale, b->cim - dy / b->scale, parent->getScale() );
}


// zoom of a factor factor and translate of dx dy
void RenderWindow::zoom ( double factor, int cutdx, int cutdy ) {
	qDebug() <<"RenderWindow::zoom()";
	
	double multiplier = 0.5 - 0.5 / factor;
	QPoint topLeft( multiplier * ( width() + 2.0 * cutdx ), multiplier * ( height() + 2.0 * cutdy ) );
	QPoint newCentre( multiplier * 2.0 * cutdx, multiplier * 2.0 * cutdy );
	
	parent->putValues( b->cre + newCentre.x() / b->scale, b->cim - newCentre.y() / b->scale, b->scale * factor );
}

bool RenderWindow::valuesChanged ( ) {
	return width() != (int) b->w || height() != (int) b->h;
}

void RenderWindow::setMouseMode( bool mode ) {
	this->zoomMode = mode;
}

