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



RenderWindow::RenderWindow ( ControlWindow* parent, Buddha* b ) : BaseRenderWindow( parent ) {
	//this->parent = parent;
	this->b = b;
	this->cre = &b->cre;
	this->cim = &b->cim;
	this->scale = &b->scale;
	this->w = &b->w;
	this->h = &b->h;
	this->image = &(b->RGBImage);


	connect( this, SIGNAL( frameRequest( ) ), b, SLOT( updateRGBImage( ) ) );
	connect( b, SIGNAL( imageCreated() ), this, SLOT( receivedFrame( ) ) );
	connect( b, SIGNAL( settedValues( ) ), this, SLOT( canRestartDrawing( ) ) );
}




void RenderWindow::paintEvent(QPaintEvent *event) {
	BaseRenderWindow::paintEvent( event );
}


void RenderWindow::resizeEvent( QResizeEvent* event ) {
	BaseRenderWindow::resizeEvent( event );

	if ( event->oldSize() != QSize(-1,-1) && !resizeSent ) {
		parent->sendValues( true );
		disabledDrawing = true;
		resizeSent = true;
	}
}


void RenderWindow::mouseReleaseEvent(QMouseEvent *event) {
	qDebug() << "RenderWindow::mouseReleaseEvent()";
	BaseRenderWindow::mouseReleaseEvent( event );
	if ( this->newcre == *cre && this->newcim == *cim && this->newscale == *scale )
		return;

	parent->setCre( this->newcre );
	parent->setCim( this->newcim );
	parent->setScale( this->newscale );
	parent->modelToGUI();
	disabledDrawing = true;
	parent->sendValues( true );
}




void RenderWindow::wheelEvent(QWheelEvent *event) {
	BaseRenderWindow::wheelEvent( event );
	parent->setCre( this->newcre );
	parent->setCim( this->newcim );
	parent->setScale( this->newscale );
	parent->modelToGUI();
	if ( parent->valuesChanged() ) disabledDrawing = true;
	parent->sendValues( true );
}

void RenderWindow::mousePressEvent(QMouseEvent *event) {
	BaseRenderWindow::mousePressEvent( event );
}


void RenderWindow::mouseMoveEvent(QMouseEvent *event) {
	BaseRenderWindow::mouseMoveEvent( event );
}










/*
void RenderWindow::scroll ( int dx, int dy ) {
	qDebug() <<"RenderWindow::scroll()";
	if ( dx == 0 && dy == 0 ) return;

	imageOffset = QPoint( 0, 0 );
	//parent->putValues( b->cre + dx / b->scale, b->cim - dy / b->scale, parent->getScale() );
	parent->setCre( b->cre + dx / b->scale );
	parent->setCim( b->cim - dy / b->scale );
	parent->modelToGUI();
}


// zoom of a factor factor and translate of dx dy
void RenderWindow::zoom ( double factor, int cutdx, int cutdy ) {
	qDebug() <<"RenderWindow::zoom()";
	
	double multiplier = 0.5 - 0.5 / factor;
	QPoint topLeft( multiplier * ( width() + 2.0 * cutdx ), multiplier * ( height() + 2.0 * cutdy ) );
	QPoint newCentre( multiplier * 2.0 * cutdx, multiplier * 2.0 * cutdy );
	
	//parent->putValues( b->cre + newCentre.x() / b->scale, b->cim - newCentre.y() / b->scale, b->scale * factor );
	parent->setCre( b->cre + newCentre.x() / b->scale );
	parent->setCim( b->cim - newCentre.y() / b->scale );
	parent->setScale( b->scale * factor );
	parent->modelToGUI();
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
}*/
