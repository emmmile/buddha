
#include <QPainter>
#include <QtGui>
#include <iostream>
#include "renderWindow.h"
#include "controlWindow.h"
#include "staticutils.h"

using namespace std;

void RenderWindow::closeEvent ( QCloseEvent* ) {
	parent->renderWinClosed( );
}



RenderWindow::RenderWindow ( ControlWindow* parent, Buddha* b ) {
	this->parent = parent;
	this->b = b;

	connect( b, SIGNAL(doneIteration(/*const QImage&*/)), this, SLOT(updateImage(/*const QImage &*/)));

	#ifndef QT_NO_CURSOR
	setCursor(Qt::CrossCursor);
	#endif
	setMouseTracking( true );
	resize( defaultWidth, defaultHeight );
	mousex = mousey = 0.0;
	setWindowIcon( parent->windowIcon() );
	
	#if ZOOM
	alpha = 0;
	haveSomething = false;
	alphaStep = 20;
        over = QImage( defaultWidth, defaultHeight, QImage::Format_ARGB32 );
	setAlpha( );
        over.fill( 0 );
	#endif

	selectionBorder = QColor( 255, 255, 255, 191 );
	selection = QColor( 255, 255, 255, 63 );
}



void RenderWindow::updateImage( ) {
	#if ZOOM
	if ( alpha > alphaStep ) 
		alpha -= alphaStep ;
	else	alpha = 0;
	
	if ( alpha != 0 ) 
		setAlpha( );
		
	haveSomething = true;
	#endif

	
	update();
}


void RenderWindow::paintEvent( QPaintEvent* ) {
	QPoint off( imageOffset.x() < 0 ? 0 : imageOffset.x(), imageOffset.y() < 0 ? 0 : imageOffset.y() );


	//ttime();
	
	QPainter painter ( this );
	painter.fillRect( rect(), Qt::black );
	
	
if ( haveSomething ) {
	b->lock(); // necessary for reading the image
	out = QImage( (uchar*) b->RGBImage, b->w, b->h, QImage::Format_RGB32 );
	painter.drawImage( off, out, rect() & rect().translated( -imageOffset ) );
	b->unlock();
}
	
	//printf( "PaintEvent 1: %lf\n", ttime() );

	#if ZOOM
	if ( alpha != 0 )
		painter.drawImage( off, over, rect() & rect().translated( -imageOffset ) );
	#endif
	
	painter.setPen( selectionBorder );
	if ( begMouse != endMouse ) {
		//painter.setBrush( selection );
		painter.drawRect( QRect( begMouse, endMouse ) );
	}
	
	painter.drawText( 2, size().height() - 2,
			  "Re: " + QString::number( mousex, 'f', 8 ) +
			  ", Im: " + QString::number( mousey, 'f', 8 ) );
	
	
	//printf( "PaintEvent 2: %lf\n", ttime() );
}



void RenderWindow::mousePressEvent(QMouseEvent *event) {

	begMouse = endMouse = event->pos();	
}


void RenderWindow::mouseMoveEvent(QMouseEvent *event) {
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
	//status->showMessage( QString::number( x ) + ", " + QString::number( y ) );
}


void RenderWindow::mouseReleaseEvent(QMouseEvent *event) {
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
		// TODO ugly
		parent->setValues( b->cre + dx / b->scale, b->cim - dy / b->scale, parent->getScale() );
		parent->render();
	}
	if ( event->button() == Qt::RightButton || event->button() == Qt::MidButton ) {
		imageOffset = endMouse - begMouse;		
		
		scroll( -imageOffset.x(), -imageOffset.y() );
		parent->render();
	}
	
	begMouse = endMouse = event->pos();
}




void RenderWindow::resizeEvent( QResizeEvent* ) {
	
	
	parent->render( );
	over = QImage( size(), QImage::Format_ARGB32 );
	over.fill( 0 );
	haveSomething = false;
	/*if ( !b->isPaused() ) {
		cout << "Quasd..\n";
		// XXX XXX
	parent->render( );
		#if ZOOM
                over = QImage( size(), QImage::Format_ARGB32 );
                over.fill( 0 );
		haveSomething = false;
		#endif
	} else {
		//if ( size().width() != parent->b->getW() || size().height() != parent->b->getH() )
		if ( parent->valuesChanged() )
			parent->setButtonStart( );
		else 	parent->setButtonResume( );
	}*/
}




void RenderWindow::keyPressEvent( QKeyEvent *event ) {
	switch (event->key()) {
		case Qt::Key_Plus:
			zoom( zoomFactor );
			parent->render();
			break;
		case Qt::Key_Minus:
			zoom( 1.0 / zoomFactor );
			parent->render();
			break;
		case Qt::Key_Left:
			scroll(-scrollStep, 0);
			parent->render();
			break;
		case Qt::Key_Right:
			scroll(+scrollStep, 0);
			parent->render();
			break;
		case Qt::Key_Down:
			scroll(0, +scrollStep);
			parent->render();
			break;
		case Qt::Key_Up:
			scroll(0, -scrollStep);
			parent->render();
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
	parent->render();
}



void RenderWindow::scroll ( int dx, int dy ) {
	if ( dx == 0 && dy == 0 ) return;

	imageOffset = QPoint( 0, 0 );

	#if ZOOM
	if ( !haveSomething ) {
		// if out is empty (I've not obtaine an image from the rendering threads, I create
		// a black image and I plot on it the one that I had before
		out = QImage( size(), QImage::Format_RGB32 );
		out.fill( 0 );
	}
	
	alpha = 255;
	
	// I draw over out (the old image, eventually nothing) and then I save the
	// interesting part like "new" over
	QPainter painter( &out );
	
	b->lock();
	painter.drawImage( 0, 0, over );
	painter.end();
	over = out.copy( QRect( QPoint( dx, dy ), size() ) ).convertToFormat( QImage::Format_ARGB32 );
	b->unlock();
	#endif
	
	
	
	haveSomething = true;
	parent->setValues( b->cre + dx / b->scale, b->cim - dy / b->scale, parent->getScale() );
	update( );
}


// zoom of a factor factor and translate of dx dy
void RenderWindow::zoom ( double factor, int cutdx, int cutdy ) {
	#if ZOOM
	double multiplier = 0.5 - 0.5 / factor;
	QPoint topLeft( multiplier * ( width() + 2.0 * cutdx ), multiplier * ( height() + 2.0 * cutdy ) );
	QPoint newCentre( multiplier * 2.0 * cutdx, multiplier * 2.0 * cutdy );
	
	// like in scroll(), if I have nothing I build a black image
	if ( !haveSomething ) {
		out = QImage( size(), QImage::Format_RGB32 );
		out.fill( 0 );
	}
		
	alpha = 255;
	b->lock();
	QPainter painter( &out );
	painter.drawImage( QPoint( 0, 0 ), over );
	painter.end();
	over = out.copy( QRect( topLeft, size() / factor ) ).convertToFormat( QImage::Format_ARGB32 ).scaled( size() );
	b->unlock();
	#endif
	
	
	haveSomething = false;
	parent->setValues( b->cre + newCentre.x() / b->scale, 
			   b->cim - newCentre.y() / b->scale,
			   b->scale * factor );
	//cout << parent->getCim() << endl;
	update( );
}


void RenderWindow::clearBuffers ( ) {
#if ZOOM
	alpha = 0;
	update( );
#endif
	b->clear( );
}

#if ZOOM
// change the transparency of the image.. it seems that the Qt doesn't have a feature like this :S
void RenderWindow::setAlpha ( ) {
	ttime();
	
	unsigned char* last = over.bits() + over.width() * over.height() * sizeof( int );
	
	
	
	if ( over.format() == QImage::Format_ARGB32_Premultiplied ) {
		//float factor = alpha / 255.0f;
		//for ( unsigned char* j = over.bits(); j < last; ) {
		//	*(j++) *= factor; *(j++) *= factor; *(j++) *= factor;  *(j++) = alpha;
		//}
		cerr << "QImage::Format_ARGB32_Premultiplied Not Supported.\n";
		return;
	}
	
	
	
	for ( unsigned char* j = over.bits() + 3; j < last + 3; j += 4 )
		*j = alpha;
}

void RenderWindow::setAlphaStep( unsigned char step ) {
	alphaStep = step;
}
#endif


















bool RenderWindow::valuesChanged ( ) {
	return width() != (int) b->w || height() != (int) b->h;
}



void RenderWindow::saveScreenshot( QString fileName ) {
	saver.set( b, fileName );
	saver.start( );
}

void RenderWindow::setMouseMode( bool mode ) {
	this->zoomMode = mode;
}
