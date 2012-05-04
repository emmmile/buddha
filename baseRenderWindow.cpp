#include "baseRenderWindow.h"

BaseRenderWindow::BaseRenderWindow(ControlWindow* parent) : QWidget() {
	this->parent = parent;

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

	selectionBorder = QColor( 255, 255, 255, 191 );
	selection = QColor( 255, 255, 255, 63 );
}


void BaseRenderWindow::closeEvent ( QCloseEvent* ) {
	//parent->renderWinClosed( );
}


void BaseRenderWindow::paintEvent( QPaintEvent* ) {
	//qDebug() << "BaseRenderWindow::paintEvent()";
	QPoint off( imageOffset.x() < 0 ? 0 : imageOffset.x(), imageOffset.y() < 0 ? 0 : imageOffset.y() );

	QPainter painter ( this );
	painter.fillRect( rect(), Qt::black );

	if ( !disabledDrawing ) {
		//qDebug() << ( rect() & rect().translated( -imageOffset ) );
		out = QImage( (uchar*) *image, *w, *h, QImage::Format_RGB32 );
		painter.drawImage( off, out, rect() & rect().translated( -imageOffset ) );
	}

	painter.setPen( selectionBorder );
	if ( begMouse != endMouse ) {
		painter.setBrush( selection );
		painter.drawRect( QRect( begMouse, endMouse ) );
	}

	painter.drawText( 2, size().height() - 2, "Re: " + QString::number( mousex, 'f', 8 ) +
			  ", Im: " + QString::number( mousey, 'f', 8 ) );
}



void BaseRenderWindow::mousePressEvent(QMouseEvent *event) {
	//qDebug() << "RenderWindow::mousePressEvent()";
	begMouse = endMouse = event->pos();
}


void BaseRenderWindow::mouseMoveEvent(QMouseEvent *event) {
	//qDebug() << "RenderWindow::mouseMoveEvent()";
	mousex = ( event->x() - 0.5 * width() ) / (*scale) + (*cre);
	mousey = ( -event->y() + 0.5 * height() ) / (*scale) + (*cim);

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


void BaseRenderWindow::mouseReleaseEvent(QMouseEvent *event) {
	qDebug() << "BaseRenderWindow::mouseReleaseEvent()";
	endMouse = event->pos();
	if ( endMouse == begMouse ) return;


	if ( event->button() == Qt::LeftButton ) {

		if ( endMouse.x() > size().width() ) endMouse.setX( size().width() );
		if ( endMouse.y() > size().height() ) endMouse.setY( size().height() );

		// calcolo il nuovo zoom
		double sx = (double) width() / fabs( ( endMouse - begMouse ).x() );
		double sy = (double) height() / fabs( ( endMouse - begMouse ).y() );
		double s = min( sx, sy );

		int dx = 0.5 * ( endMouse.x() + begMouse.x() - width() );
		int dy = 0.5 * ( height() - endMouse.y() - begMouse.y() );


		scroll( dx, dy );
		//zoom( s );
		this->newscale = (*scale) * s;
		//this->newcre = newcre + dx / this->newscale;
		//this->newcim = newcim + dy / this->newscale;
		//parent->setCre( b->cre + dx / b->scale );
		//parent->setCim( b->cim + dy / b->scale );
		//parent->modelToGUI();
		//parent->sendValues( true );
		//disabledDrawing = true;
	}
	if ( event->button() == Qt::RightButton || event->button() == Qt::MidButton ) {
		imageOffset = endMouse - begMouse;

		scroll( -imageOffset.x(), imageOffset.y() );
		//parent->sendValues( true );
		//disabledDrawing = true;
	}

	begMouse = endMouse = event->pos();
}


void BaseRenderWindow::resizeEvent( QResizeEvent* ) {
	//qDebug() << "RenderWindow::resizeEvent()";
	//qDebug() << "New size: " << resize->size() << ", old size: " << resize->oldSize();
}




void BaseRenderWindow::wheelEvent(QWheelEvent *event) {
	double factor = event->delta() > 0 ? zoomFactor : 1.0 / zoomFactor;
	int dx, dy;

	if ( !zoomMode ) {
		dx = event->x() - width() * 0.5;
		dy = event->y() - height() * 0.5;
	} else	dx = dy = 0;

	zoom( factor, dx, dy );
}





















void BaseRenderWindow::sendFrameRequest ( ) {
	//qDebug() << "BaseRenderWindow::sendFrameRequest()";

	// this only tests if we already sent a frame request, otherwise the request
	// accumulates on the buddha thread side, and slows a lot the responsivity also of the GUI
	if ( !alreadySent ) {
		emit frameRequest( );
		alreadySent = true;
	}
}


void BaseRenderWindow::receivedFrame( ) {
	//qDebug() << "BaseRenderWindow::receivedFrame()";
	// a frame has been calculated by the buddha thread, simply I force a repaint
	alreadySent = false;
	update( );
}

// XXX maybe change the name... this can be received also after a resize
void BaseRenderWindow::canRestartDrawing( ) {
	disabledDrawing = false;
	//qDebug() << "canRestartDrawing()" << disabledDrawing << (void*) this;

	// this is a strategy to don't resize the workers at every single resize event
	// i enable the sending of "resize requests" only if I processed the previous one
	resizeSent = false;
	// so, from a resizeEvent to another, there can be a lot of them that has been not
	// considered, so when I received a "resize done" from the buddha thread a manually
	// control if the sizes are equal, and if not I send a new request
	if ( size().height() != (int) *h || size().width() != (int) *w )
		this->resizeEvent( new QResizeEvent( size(), QSize( *w, *h ) ) );
}


void BaseRenderWindow::scroll ( int dx, int dy ) {
	qDebug() <<"BaseRenderWindow::scroll()";
	if ( dx == 0 && dy == 0 ) return;

	imageOffset = QPoint( 0, 0 );
	this->newcre = (*cre) + dx / (*scale);
	this->newcim = (*cim) + dy / (*scale);
	this->newscale = (*scale);
}


// zoom of a factor factor and translate of dx dy
void BaseRenderWindow::zoom ( double factor, int cutdx, int cutdy ) {
	//qDebug() <<"BaseRenderWindow::zoom()";

	double multiplier = 0.5 - 0.5 / factor;
	QPoint newCentre( multiplier * 2.0 * cutdx, multiplier * 2.0 * cutdy );

	this->newcre = (*cre) + newCentre.x() / (*scale);
	this->newcim = (*cim) - newCentre.y() / (*scale);
	this->newscale = (*scale) * factor;
}

void BaseRenderWindow::setMouseMode( bool mode ) {
	this->zoomMode = !mode;
}
