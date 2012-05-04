
#include "demoWindow.h"

#if DEMO_WINDOW

DemoWindow::DemoWindow( ControlWindow *parent, Buddha *b ) : BaseRenderWindow( parent ) {
	this->b = b;
	this->cre = &b->democre;
	this->cim = &b->democim;
	this->scale = &b->demoscale;
	this->w = &b->demow;
	this->h = &b->demoh;
	this->image = &b->demoImage;

	b->setDemo( b->cre, b->cim, b->scale, this->size(), true );

	//connect( this, SIGNAL( frameRequest( ) ), b, SLOT( updateRGBImage() ) );
	connect( b, SIGNAL( imageCreated() ), this, SLOT( receivedFrame( ) ) );
	connect( b, SIGNAL( demoSettedValues( ) ), this, SLOT( canRestartDrawing( ) ) );

}


void DemoWindow::paintEvent(QPaintEvent *event) {
	BaseRenderWindow::paintEvent( event );

	//qDebug() << *cre << *cim << *scale << *w << *h;
	//qDebug() << disabledDrawing << (void*) this;
}

void DemoWindow::resizeEvent(QResizeEvent *event) {
	BaseRenderWindow::resizeEvent( event );
}


void DemoWindow::mouseReleaseEvent(QMouseEvent *event) {
	//qDebug() << "RenderWindow::mouseReleaseEvent()";
	BaseRenderWindow::mouseReleaseEvent( event );
	if ( this->newcre == *cre && this->newcim == *cim && this->newscale == *scale )
		return;

	disabledDrawing = true;
	b->setDemo( this->newcre, this->newcim, this->newscale, this->size(), true );
}




void DemoWindow::wheelEvent(QWheelEvent *event) {
	BaseRenderWindow::wheelEvent( event );

	disabledDrawing = true;
	b->setDemo( this->newcre, this->newcim, this->newscale, this->size(), true );
}

void DemoWindow::mousePressEvent(QMouseEvent *event) {
	BaseRenderWindow::mousePressEvent( event );
}


void DemoWindow::mouseMoveEvent(QMouseEvent *event) {
	BaseRenderWindow::mouseMoveEvent( event );
}









#endif
