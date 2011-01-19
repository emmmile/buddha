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


#include "controlWindow.h"
#include <QVBoxLayout>
#include <QtGui>
#include <iostream>



ControlWindow::ControlWindow ( )  {
	cre = initialCre;
	cim = initialCim;
	scale = initialScale;
	red = initialRed;
	green = initialGreen;
	blue = initialBlue;
	contrast = loadedContrast = initialContrast;
	lightness = loadedLightness = initialLight;
	fps = initialFps;
	step = 10 / scale;
	
	//timer = new QTimer( this );
	b = new Buddha( );
	icon = new QIcon( "resources/icon.png" );
	setWindowIcon( *icon );

	centralWidget = new QWidget( this );
	createGraphBox();
	createRenderBox();
	createControlBox();
	createMenus();
	
	renderWin = new RenderWindow( this, b );
	QHBoxLayout *hbox = new QHBoxLayout( );
	QVBoxLayout *vbox = new QVBoxLayout( );
	vbox->addWidget( renderBox );
	vbox->addWidget( buttonsBox );
	hbox->addWidget( graphBox );
	hbox->addLayout( vbox );
	centralWidget->setLayout( hbox );
	setCentralWidget( centralWidget );
	setMenuBar( menuBar );

	resize( 600, 400 );
	//step = scale / 1000;
	
	//setLightness( lightSlider->value() );
	setColorSliders( red, green, blue );
	setImageSliders( lightness, contrast, fps );
	
	setRedIterationDepth( red );
	setGreenIterationDepth( green );
	setBlueIterationDepth( blue );
	
	setLightness( initialLight );
	setContrast( initialContrast );
	putValues ( cre, cim, scale );
	setFps( initialFps );

	connect( greenSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setGreenIterationDepth( int ) ) );
	connect( blueSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setBlueIterationDepth( int ) ) );
	connect( redSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setRedIterationDepth( int ) ) );
	connect( reBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCre( double ) ) );
	connect( imBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCim( double ) ) );
	connect( zoomBox, SIGNAL( valueChanged( double ) ), this, SLOT( setScale( double ) ) );

	connect( lightSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setLightness( int ) ) );
	connect( contrastSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setContrast( int ) ) );
	connect( fpsSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setFps( int ) ) );
	connect( threadsSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setThreadNum( int ) ) );
	connect( startButton, SIGNAL( clicked() ), this, SLOT(handleStartButton()));
	//connect( currentButton, SIGNAL( clicked() ), this, SLOT( handleCurrentButton() ) );
	connect( resetButton, SIGNAL( clicked() ), this, SLOT( handleResetButton() ) );
	//connect( defaultButton, SIGNAL( clicked() ), this, SLOT( handleDefaultButton() ) );
	connect( normalZoom, SIGNAL( toggled( bool) ), renderWin, SLOT( setMouseMode( bool ) ) );
	
	// i set some shortcuts also for renderWin
	connect( new QShortcut( startButton->shortcut(), renderWin ), SIGNAL(activated()), startButton, SLOT(animateClick()) );
	connect( new QShortcut( resetButton->shortcut(), renderWin ), SIGNAL(activated()), resetButton, SLOT(animateClick()) );
	connect( new QShortcut( screenShotAct->shortcut(), renderWin ), SIGNAL(activated()), this, SLOT(saveScreenshot()) );
	
	connect( this, SIGNAL( setValues( double, double, double, unsigned int, unsigned int, unsigned int, QSize, bool ) ), 
		 b, SLOT( set( double, double, double, unsigned int, unsigned int, unsigned int, QSize, bool ) ) );

	connect( this, SIGNAL( startCalculation( ) ), b, SLOT( startGenerators( ) ) );
	connect( this, SIGNAL( stopCalculation( ) ), b, SLOT( stopGenerators( ) ) );
	connect( this, SIGNAL( pauseCalculation( ) ), b, SLOT( pauseGenerators( ) ) );
	connect( this, SIGNAL( clearBuffers( ) ), b, SLOT( clearBuffers( ) ) );
	connect( this, SIGNAL( changeThreadNumber( int ) ), b, SLOT( changeThreadNumber( int ) ) );
	setThreadNum( threadsSlider->value() );

	// these are for the real-time update of the values directly from the controlWindow
	connect( reBox, SIGNAL( valueChanged( double ) ), this, SLOT( sendValues( ) ) );
	connect( imBox, SIGNAL( valueChanged( double ) ), this, SLOT( sendValues( ) ) );
	connect( zoomBox, SIGNAL( valueChanged( double ) ), this, SLOT( sendValues( ) ) );
	connect( greenSlider, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
	connect( blueSlider, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
	connect( redSlider, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
}



void ControlWindow::createGraphBox ( ) {
	graphBox = new QGroupBox( tr( "Graph quality" ), this );
	
	iterationRedLabel = new QLabel( graphBox );
	redSlider = new QSlider( graphBox );
	redSlider->setOrientation(Qt::Horizontal);
	redSlider->setMaximum( maxDepth );
	redSlider->setSliderPosition( initialRed );
	redSlider->setToolTip( "After how many iterations a red point will be drawn?" );
	iterationGreenLabel = new QLabel( graphBox );
	greenSlider = new QSlider( graphBox );
	greenSlider->setOrientation(Qt::Horizontal);
	greenSlider->setMaximum( maxDepth );
	greenSlider->setSliderPosition( initialGreen );
	greenSlider->setToolTip( "After how many iterations a green point will be drawn?" );
	iterationBlueLabel = new QLabel( graphBox );
	blueSlider = new QSlider( graphBox );
	blueSlider->setOrientation(Qt::Horizontal);
	blueSlider->setMaximum( maxDepth );
	blueSlider->setSliderPosition( initialBlue );
	blueSlider->setToolTip( "After how many iterations a blue point will be drawn?" );
	
	reLabel = new QLabel( "Real Center (-2.0 ~ 2.0):", graphBox );
	reBox = new QDoubleSpinBox( graphBox );
	reBox->setAlignment(Qt::AlignCenter);
	reBox->setRange( minRe, maxRe );
	reBox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	reBox->setAccelerated( true );
	reBox->setSingleStep( step );
	reBox->setDecimals( PRECISION );
	reBox->setToolTip( "Specify the real centre of the image" );
	imLabel = new QLabel( "Imaginary Center (-2.0 ~ 2.0):", graphBox );
	imBox = new QDoubleSpinBox( graphBox );
	imBox->setAlignment(Qt::AlignCenter);
	imBox->setRange( minIm, maxIm );
	imBox->setSingleStep( step );
	imBox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	imBox->setAccelerated( true );
	imBox->setDecimals( PRECISION );
	imBox->setToolTip( "Specify the imaginary centre of the image" );
	
	
	
	zoomLabel = new QLabel( "Magnification (100 ~ " + QString::number( maxScale, 'g', 0 ) + "):", graphBox );
	zoomBox = new QDoubleSpinBox( graphBox );
	zoomBox->setAlignment(Qt::AlignCenter);
	zoomBox->setSingleStep( 10 );
	zoomBox->setRange( minScale, maxScale );
	zoomBox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	zoomBox->setAccelerated( true );
	zoomBox->setDecimals( PRECISION / 3 );
	zoomBox->setToolTip( "Specify the magnification level of the rendered image" );
	
	
	QVBoxLayout *vbox = new QVBoxLayout ( );
	

	
	
	vbox->addWidget( iterationRedLabel );
	vbox->addWidget( redSlider );
	vbox->addWidget( iterationGreenLabel );
	vbox->addWidget( greenSlider );
	vbox->addWidget( iterationBlueLabel );
	vbox->addWidget( blueSlider );
	//vbox->addStretch(1);
	vbox->addWidget( reLabel );
	vbox->addWidget( reBox );
	vbox->addWidget( imLabel );
	vbox->addWidget( imBox );
	vbox->addWidget( zoomLabel );
	vbox->addWidget( zoomBox );
	//vbox->addStretch(1);
        graphBox->setLayout( vbox );


}




void ControlWindow::createRenderBox ( ) {
	renderBox = new QGroupBox( "Render quality", this );
	
	contrastLabel = new QLabel( "Contrast:", renderBox );
	contrastSlider = new QSlider( renderBox );
	contrastSlider->setMaximum( maxContrast );
	contrastSlider->setOrientation(Qt::Horizontal);
	
	lightLabel = new QLabel( "Lightness:", renderBox );
	lightSlider = new QSlider( renderBox );
	lightSlider->setMaximum( maxLightness );
	lightSlider->setOrientation(Qt::Horizontal);
	
	fpsLabel = new QLabel( "Frames per second:", renderBox );
	fpsSlider = new QSlider( renderBox );
	fpsSlider->setMinimum( 0 );
	fpsSlider->setMaximum( maxFps );
	fpsSlider->setOrientation(Qt::Horizontal);
	
	threadsLabel = new QLabel( "Threads:", renderBox );
	threadsSlider = new QSlider( renderBox );
	threadsSlider->setMinimum( 1 );
	threadsSlider->setMaximum( QThread::idealThreadCount() );
	threadsSlider->setOrientation(Qt::Horizontal);
	updateThreadLabel( QThread::idealThreadCount() );
	threadsSlider->setValue( QThread::idealThreadCount() );
	
	QVBoxLayout *vbox = new QVBoxLayout ( );
	vbox->addWidget( contrastLabel );
	vbox->addWidget( contrastSlider );
	vbox->addWidget( lightLabel );
	vbox->addWidget( lightSlider );
	vbox->addWidget( fpsLabel );
	vbox->addWidget( fpsSlider );
	vbox->addWidget( threadsLabel );
	vbox->addWidget( threadsSlider );
//	vbox->addStretch(1);
	renderBox->setLayout( vbox );

}

void ControlWindow::createActions ( ) {
	exitAct = new QAction(tr("E&xit"), this);
	exitAct->setShortcut(tr("Ctrl+Q"));
	connect(exitAct, SIGNAL(triggered()), this, SLOT(close()));

	aboutAct = new QAction(tr("&About Buddha++"), this);
	connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

	aboutQtAct = new QAction(tr("About &Qt"), this);
	connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
	
	screenShotAct = new QAction( "Save Screenshot", this );
	screenShotAct->setShortcut( tr( "Ctrl+S" ) );
	screenShotAct->setIcon( QIcon( "resources/save.png" ) );
	screenShotAct->setEnabled( false );
	connect(screenShotAct, SIGNAL(triggered()), this, SLOT(saveScreenshot()));
	connect( this, SIGNAL(screenshotRequest( QString ) ), b, SLOT( saveScreenshot(QString) ) );

	saveAct = new QAction( "Save Configuration", this );
	saveAct->setShortcut( tr( "Alt+Ctrl+S" ) );
	saveAct->setIcon( screenShotAct->icon() );
	connect(saveAct, SIGNAL(triggered()), this, SLOT(saveConfig()));

	openAct = new QAction( "Open Configuration", this );
	openAct->setShortcut( tr( "Alt+Ctrl+O" ) );
	openAct->setIcon( QIcon("resources/open.png") );
	connect(openAct, SIGNAL(triggered()), this, SLOT(openConfig()));

}

void ControlWindow::createMenus ( ) {
	createActions( );
	menuBar = new QMenuBar( this );
	fileMenu = new QMenu(tr("&File"), menuBar );
	fileMenu->addAction(openAct);
	fileMenu->addAction(saveAct);
	fileMenu->addAction(screenShotAct);
	fileMenu->addSeparator();
	fileMenu->addAction(exitAct);

	helpMenu = new QMenu(tr("&Help"), menuBar );
	helpMenu->addAction(aboutAct);
	helpMenu->addAction(aboutQtAct);

	menuBar->addMenu(fileMenu);
	//menuBar->addMenu(viewMenu);
	menuBar->addMenu(helpMenu);
}

void ControlWindow::createControlBox ( ) {
	
	buttonsBox = new QGroupBox( "Controls", this );
	//currentButton = new QPushButton( tr( "&Current" ), buttonsBox );
	startButton = new QPushButton( tr( "&Start" ), buttonsBox );
	resetButton = new QPushButton( tr( "&Reset" ), buttonsBox );
	//defaultButton = new QPushButton( tr( "&Default" ), buttonsBox );
	startButton->setToolTip( "Press to start/resume/stop rendering" );
	//defaultButton->setToolTip( "Press to set deafult rendering values" );
	resetButton->setToolTip( "Press to cancel the rendered image" );
	//currentButton->setToolTip( "Press to set current rendering values" );
	QFont f;
	f.setBold( true );
	startButton->setFont( f );
	resetButton->setDisabled( true );
	
	mouseLabel = new QLabel( "Mouse zoom mode:", buttonsBox );
	normalZoom = new QRadioButton( "On center", buttonsBox );
	mouseZoom = new QRadioButton( "On cursor", buttonsBox );
	QVBoxLayout *vbox = new QVBoxLayout ( );
	QHBoxLayout* radioLayout = new QHBoxLayout( );
	//QHBoxLayout *buttonsLayout = new QHBoxLayout ( );
	radioLayout->addWidget( normalZoom );
	radioLayout->addWidget( mouseZoom );

	//buttonsLayout->addWidget( currentButton );
	//buttonsLayout->addWidget( resetButton );

	//vbox->addLayout( buttonsLayout );
	vbox->addWidget( resetButton );
	vbox->addWidget( startButton );
	//vbox->addStretch(1);
	vbox->addWidget( mouseLabel );
	vbox->addLayout( radioLayout );
	buttonsBox->setLayout( vbox );

	
	mouseZoom->setChecked( true );
	normalZoom->setChecked( false );
	
	connect( b, SIGNAL( stoppedGenerators( bool ) ), startButton, SLOT( setEnabled( bool ) ) );
	connect( b, SIGNAL( stoppedGenerators( bool ) ), resetButton, SLOT( setDisabled( bool ) ) );
	connect( b, SIGNAL( startedGenerators( bool ) ), resetButton, SLOT( setEnabled( bool ) ) );
	connect( b, SIGNAL( startedGenerators( bool ) ), startButton, SLOT( setDisabled( bool ) ) );
}


void ControlWindow::sendValues ( bool pause ) {
	if ( this->valuesChanged() )
		emit setValues( cre, cim, scale, highr, highg, highb, renderWin->size(), pause );
}





//// BUTTON HANDLING
void ControlWindow::handleStartButton ( ) {
	qDebug() << "ControlWindow::handleStartButton(), thread " << QThread::currentThreadId();
	
	
	emit sendValues( false );
	emit startCalculation( );
	renderWin->timer->start( sleepTime );
	
	
	red = redSlider->value();
	green = greenSlider->value();
	blue = blueSlider->value();
	screenShotAct->setEnabled( true );
	
	if ( renderWin->isHidden() ) 
		renderWin->show();
}


void ControlWindow::handleResetButton ( ) {
	renderWin->repaint();
	//renderWin->clearBuffers();
	emit clearBuffers( );
}

bool ControlWindow::valuesChanged ( ) {
	
	return  cre != b->cre || cim != b->cim || scale != b->scale ||
		highr != b->highr || highg != b->highg || highb != b->highb ||
		renderWin->valuesChanged();
}


void ControlWindow::setColorSliders ( int r, int g, int b ) {
	redSlider->setSliderPosition( r );
	greenSlider->setSliderPosition( g );
	blueSlider->setSliderPosition( b );
}

void ControlWindow::setImageSliders ( int l, int c, int f ) {
	lightSlider->setValue( l );
	contrastSlider->setValue( c );
	fpsSlider->setValue( f );
}

void ControlWindow::putValues ( double cre, double cim, double scale ) {
	reBox->setValue( cre );
	imBox->setValue( cim );
	zoomBox->setValue( scale );
	
	//this->cre = cre;
	//this->cim = cim;
	//this->scale = scale;
	setCre( cre );
	setCim( cim );
	setScale( scale );
	//viewStartButton();
}











// FUNCTIONS FOR THE INPUT WIDGETS

void ControlWindow::updateRedLabel( ) {
	iterationRedLabel->setText( "Red iteration depth: [" + QString::number( highr ) + "]" );
}

void ControlWindow::updateGreenLabel( ) {
	iterationGreenLabel->setText( "Green iteration depth: [" + QString::number( highg ) + "]" );
}

void ControlWindow::updateBlueLabel( ) {
	iterationBlueLabel->setText( "Blue iteration depth: [" + QString::number( highb ) + "]" );
}

void ControlWindow::updateFpsLabel( ) {
	fpsLabel->setText( "Frames per second: [" + QString::number( fps / 10.0, 'f', 1 ) + "]" );
}

void ControlWindow::updateThreadLabel( int value ) {
	threadsLabel->setText( "Threads: [" + QString::number( value ) + "]" );
}

void ControlWindow::setRedIterationDepth ( int value ) {
	highr = (int) pow( 2.0, value / 2.0 );
	updateRedLabel( );
	//viewStartButton ( );
}

void ControlWindow::setGreenIterationDepth ( int value ) {
	highg = (int) pow( 2.0, value / 2.0 );
	updateGreenLabel( );
	//viewStartButton ( );
}

void ControlWindow::setBlueIterationDepth ( int value ) {
	highb = (int) pow( 2.0, value / 2.0 );
	updateBlueLabel( );
	//viewStartButton ( );
}

void ControlWindow::setLightness ( int value ) {
	lightness = value;
	//b->setLightness( (double) value / ( lightSlider->maximum() - value + 1 ) );
	//qDebug() <<"Lightness: %d %lf\n", value,(double) value / ( lightSlider->maximum() - value ) );
	b->setLightness( value );
}

void ControlWindow::setContrast ( int value ) {
	contrast = value;
	// ottengo un valore fra 0.0 e 2.0
	//b->setContrast( (double) value / contrastSlider->maximum() * 2.0 );
	b->setContrast( value );
}




void ControlWindow::setFps ( int value ) {
	fps = value;
	float toSet = ( ( fps == 0 ) ? 0.0 : fps / 10.0 );
	sleepTime = (toSet == 0.0) ? 0x0FFFFFFF : 1000.0f / toSet;
	updateFpsLabel( );
	
	if ( renderWin->timer->isActive() ) { renderWin->timer->stop(); renderWin->timer->start( sleepTime ); }
}

void ControlWindow::setCre ( double d ) {
	cre = d;
	if ( cre < minRe ) cre = minRe;
	if ( cre > maxRe ) cre = maxRe;
	//viewStartButton ( );
}

void ControlWindow::setCim ( double d ) {
	cim = d;
	if ( cim < minIm ) cim = minIm;
	if ( cim > maxIm ) cim = maxIm;
	//viewStartButton ( );
}

void ControlWindow::setScale ( double d ) {
	scale = d;
	
	if ( scale < minScale ) scale = minScale;
	if ( scale > maxScale ) scale = maxScale;
	step = 10 / scale;
	imBox->setSingleStep( step );
	reBox->setSingleStep( step );
	//viewStartButton ( );
}

void ControlWindow::setThreadNum ( int value ) {
	updateThreadLabel( value );
	emit changeThreadNumber( value );
}














// UTILITY FUNCTIONS


void ControlWindow::renderWinClosed ( ) {	
	screenShotAct->setEnabled( false );
	renderWin->timer->stop( );
	
	// here an asynchronous termination is sufficient
	emit stopCalculation( );
	emit clearBuffers( );
}

void ControlWindow::closeEvent ( QCloseEvent* ) {
	exit( );
}

void ControlWindow::exit ( ) {
	// close the rendering window
	renderWin->close();
	// XXX I think this is not completely correct because also if the stopCalculation()
	// has been sent, i'm not shure that it has been executed by the buddha thread
	// so I should wait for the signal stoppedCalculation and then this two lines
	// that stops the buddha event loop
	b->exit();
	b->wait();
}

void ControlWindow::about ( ) {
	QMessageBox::about( this, tr("About Buddha++"),
		tr( "<p>The <b>Buddha++</b> viewer is a program that let you "
		    "zoom into the BuddhaBrot fractal, a particular view of the "
		    "Mandelbrot set (<a href=\"http://en.wikipedia.org/wiki/Mandelbrot_set\">"
		    "http://en.wikipedia.org/wiki/Mandelbrot_set</a>).</p>"
		    "<p>Some general informations about the BuddhaBrot can be found at "
		    "<a href=\"http://en.wikipedia.org/wiki/Buddhabrot\">"
		    "http://en.wikipedia.org/wiki/Buddhabrot</a>.</p>"
		    "If you want to know everything about the Mendlbrot set take a tour in "
		    "<a href=\"http://www.mrob.com/pub/muency.html\">Mu-Ency -- The Encyclopedia of "
		    "the Mandelbrot Set</a>.</p>"
		    "<p><b>Buddha++</b> has been written by Emilio Del Tessandoro and compiled "
		    "with flags \"<b>" xstr(FLAGS) "\"</b>.</p>" ) );
}



void ControlWindow::saveScreenshot ( ) {
	// simply opens a dialog and send a save request to the buddha thread
	QString name = "[" + QString::number( cre ) + ", " + QString::number( cim ) + ", " + QString::number( scale ) + "].png";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Screenshot"), 
			   "./" + name, tr("Image Files (*.png)"));
			   
	emit screenshotRequest( fileName );
}






// TODO these functions are completely to review
void ControlWindow::saveConfig ( ) {
	QString name = "[" + QString::number( this->cre ) + ", " + QString::number(cim ) + "].Buddha++";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Current Config"), "./" + name,
			   tr("Buddha++ Files (*.Buddha++)") );
	/*if ( fileName.isNull() ) return;
	QFile file( fileName );
	file.open(QIODevice::WriteOnly);
	QDataStream out( &file );

	out << red << green << blue << cre << cim << scale << lightness << contrast << renderWin->size();
	file.close();*/
}


void ControlWindow::openConfig ( ) {
	QString fileName = QFileDialog::getOpenFileName( this, tr("Open"),
			   "./", tr("Buddha++ Files (*.Buddha++)") );

	/*if ( fileName.isNull() ) return;
	QFile file( fileName );
	file.open(QIODevice::ReadOnly);
	QDataStream in( &file );

	double r, i, s;
	int rr, gg, bb;
	int ll, cc;
	QSize ws;

	//in >> rr >> gg >> bb >> r >> i >> s >> pp >> ws;
	in >> rr >> gg >> bb >> r >> i >> s >> ll >> cc >> ws;
	if ( in.status() != QDataStream::Ok ) {
		QMessageBox::information( this, "Error", "Error Reading the file. Nothing has been loaded." );
		return;
	}

	this->setColorSliders(rr, gg, bb);
	loadedContrast = cc;
	loadedLightness = ll;
	this->setImageSliders( ll, cc, initialFps );*/
}
