#ifndef BASERENDERWINDOW_H
#define BASERENDERWINDOW_H

#include <QWidget>
#include "controlWindow.h"
#include "buddha.h"

//class Buddha;
//class ControlWindow;
static const int scrollStep = 100;
static const int defaultWidth = 800;
static const int defaultHeight = 600;
static const double zoomFactor = 2.0;

class BaseRenderWindow : public QWidget {
	Q_OBJECT
protected:
	ControlWindow* parent;

	double* cre, *cim, *scale;	// this is to remain generic in respect to what I'm actually drawing
	uint* w, *h, **image;
	double newcre, newcim, newscale;	// this values will be sent to buddha/controlwindow


	bool disabledDrawing;	// sometimes I don't want to draw because I'm waitinf for a signal from buddha
	QImage out;		// last frame
	bool alreadySent;
	bool resizeSent;
	QColor selection, selectionBorder;
	bool zoomMode;
	double mousex;
	double mousey;
	QPoint begMouse;
	QPoint endMouse;
	QPoint imageOffset;


	void paintEvent(QPaintEvent *event);
	void resizeEvent(QResizeEvent *);
	//void keyPressEvent(QKeyEvent *event);
	void wheelEvent(QWheelEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void closeEvent(QCloseEvent* event );

	void scroll ( int dx, int dy );
	void zoom ( double f, int cutdx = 0, int cutdy = 0 );
public:
	BaseRenderWindow( ControlWindow *parent);
public slots:
	void receivedFrame( );
	void setMouseMode( bool );
	void sendFrameRequest( );
	void canRestartDrawing( );
signals:
	void frameRequest( );
};

#endif // BASERENDERWINDOW_H
