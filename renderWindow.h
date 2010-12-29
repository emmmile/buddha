
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


