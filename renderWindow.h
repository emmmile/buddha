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


#ifndef RENDERWINDOW_H
#define RENDERWINDOW_H


#include <QMainWindow>
#include <QWidget>
#include <QStatusBar>
#include "buddha.h"
#include "pngSaver.h"
//#include "ControlWindow.h"



class QAction;
class QLabel;
class QMenu;
class QScrollArea;
class QScrollBar;
class ControlWindow;





class RenderWindow : public QWidget {
	Q_OBJECT
private:
	QImage out;
	ControlWindow* parent;
	//QStatusBar* status;
	QColor selection, selectionBorder;
	bool zoomMode;
	bool drawed;
	double mousex;
	double mousey;
	QPoint begMouse;
	QPoint endMouse;
	QPoint imageOffset;
	static const int scrollStep = 100;
	static const int defaultWidth = 800;
	static const int defaultHeight = 600;
	static const double zoomFactor = 2.0;
	Buddha* b;
	void scroll ( int dx, int dy );
	void zoom ( double f, int cutdx = 0, int cutdy = 0 );
	
	PNGSaver saver;
	
	#if ZOOM
	unsigned char alpha;
	unsigned char lastAlpha;
	QImage over;
	bool haveSomething;
	void setAlpha( );
	unsigned char alphaStep;
	#endif
public slots:
	void updateImage( );
	void setMouseMode( bool );
public:
	

	RenderWindow( ControlWindow* parent, Buddha* b );
	
	bool valuesChanged( );
	void clearBuffers ( );
	

	void saveScreenshot( QString fileName );
	#if ZOOM
	void setAlphaStep( unsigned char );
	#endif
protected:
	void paintEvent(QPaintEvent *event);
	void resizeEvent(QResizeEvent *event);
	void keyPressEvent(QKeyEvent *event);
	void wheelEvent(QWheelEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void closeEvent(QCloseEvent* event );
	
};

#endif


