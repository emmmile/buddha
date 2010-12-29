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
#include "staticutils.h"
#include <QVBoxLayout>
#include <QtGui>
#include <iostream>



ControlWindow::~ControlWindow ( ) {
	_print( "ControlWindow destructor...");
	
	delete b;
	delete icon;
	delete renderWin;
}

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

	resize( 500, 400 );
	//step = scale / 1000;
	
	//setLightness( lightSlider->value() );
	setColorSliders( red, green, blue );
	setImageSliders( lightness, contrast, fps );
	
	setRedIterationDepth( red );
	setGreenIterationDepth( green );
	setBlueIterationDepth( blue );
	
	setLightness( initialLight );
	setContrast( initialContrast );
	setAlgorithm( false );
	setValues ( cre, cim, scale );
	setFps( initialFps );


	connect( greenSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setGreenIterationDepth( int ) ) );
	connect( blueSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setBlueIterationDepth( int ) ) );
	connect( redSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setRedIterationDepth( int ) ) );
	connect( reBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCre( double ) ) );
	connect( imBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCim( double ) ) );
	connect( zoomBox, SIGNAL( valueChanged( double ) ), this, SLOT( setScale( double ) ) );
	connect( normalRadio, SIGNAL( toggled( bool) ), this, SLOT( setAlgorithm( bool ) ) );

	connect( lightSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setLightness( int ) ) );
	connect( contrastSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setContrast( int ) ) );
	connect( fpsSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setFps( int ) ) );
	connect( startButton, SIGNAL( clicked() ), this, SLOT(handleStartButton()));
	connect( currentButton, SIGNAL( clicked() ), this, SLOT( handleCurrentButton() ) );
	connect( resetButton, SIGNAL( clicked() ), this, SLOT( handleResetButton() ) );
	connect( defaultButton, SIGNAL( clicked() ), this, SLOT( handleDefaultButton() ) );
	connect( normalZoom, SIGNAL( toggled( bool) ), renderWin, SLOT( setMouseMode( bool ) ) );
	
	// alcuni shortcut li setto anche per renderwin XXX e per deallocarli?
	connect( new QShortcut( startButton->shortcut(), renderWin ), SIGNAL(activated()), startButton, SLOT(animateClick()) );
	connect( new QShortcut( defaultButton->shortcut(), renderWin ), SIGNAL(activated()), defaultButton, SLOT(animateClick()) );
	connect( new QShortcut( currentButton->shortcut(), renderWin ), SIGNAL(activated()), currentButton, SLOT(animateClick()) );
	connect( new QShortcut( resetButton->shortcut(), renderWin ), SIGNAL(activated()), resetButton, SLOT(animateClick()) );
	connect( new QShortcut( screenShotAct->shortcut(), renderWin ), SIGNAL(activated()), this, SLOT(saveScreenshot()) );
}


void ControlWindow::createGraphBox ( ) {
	graphBox = new QGroupBox( tr( "Graph quality" ), this );
	
	iterationRedLabel = new QLabel( graphBox );
	redSlider = new QSlider( graphBox );
	redSlider->setOrientation(Qt::Horizontal);
	redSlider->setMaximum( 40 );
	redSlider->setSliderPosition( initialRed );
	redSlider->setToolTip( "After how many iterations a red point will be drawn?" );
	iterationGreenLabel = new QLabel( graphBox );
	greenSlider = new QSlider( graphBox );
	greenSlider->setOrientation(Qt::Horizontal);
	greenSlider->setMaximum( 40 );
	greenSlider->setSliderPosition( initialGreen );
	greenSlider->setToolTip( "After how many iterations a green point will be drawn?" );
	iterationBlueLabel = new QLabel( graphBox );
	blueSlider = new QSlider( graphBox );
	blueSlider->setOrientation(Qt::Horizontal);
	blueSlider->setMaximum( 40 );
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
	

	
	algoLabel = new QLabel( "Algorithm:", graphBox );
	normalRadio = new QRadioButton(tr("&Naive"), graphBox );
	metropolisRadio = new QRadioButton(tr("&Metropolis"), graphBox );
	metropolisRadio->setChecked(true);
	
	QVBoxLayout *vbox = new QVBoxLayout ( );
	QHBoxLayout *radioLayout = new QHBoxLayout ( );
	radioLayout->addWidget( normalRadio );
	radioLayout->addWidget( metropolisRadio );
	

	
	
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
	vbox->addWidget( algoLabel );
	vbox->addLayout( radioLayout );
	//vbox->addStretch(1);
        graphBox->setLayout( vbox );


}




void ControlWindow::createRenderBox ( ) {
	renderBox = new QGroupBox( "Render quality", this );
	
	contrastLabel = new QLabel( "Contrast:", renderBox );
	contrastSlider = new QSlider( renderBox );
	contrastSlider->setMaximum( maxContrast );
	//contrastSlider->setValue( contrast );
	contrastSlider->setOrientation(Qt::Horizontal);
	
	lightLabel = new QLabel( "Lightness:", renderBox );
	lightSlider = new QSlider( renderBox );
	lightSlider->setMaximum( maxLightness );
	//lightSlider->setValue( lightness );
	lightSlider->setOrientation(Qt::Horizontal);
	
	fpsLabel = new QLabel( "Frames per second:", renderBox );
	fpsSlider = new QSlider( renderBox );
	fpsSlider->setMinimum( 0 );
	fpsSlider->setMaximum( maxFps );
	fpsSlider->setOrientation(Qt::Horizontal);
	
	threadsLabel = new QLabel( "Threads:", renderBox );
	threadsSlider = new QSlider( renderBox );
	threadsSlider->setMinimum( 1 );
	threadsSlider->setMaximum( 4 );
	threadsSlider->setOrientation(Qt::Horizontal);
	
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
	connect(exitAct, SIGNAL(triggered()), this, SLOT(exit()));

	aboutAct = new QAction(tr("&About Buddha++"), this);
	connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

	aboutQtAct = new QAction(tr("About &Qt"), this);
	connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
	
	screenShotAct = new QAction( "Save Screenshot", this );
	screenShotAct->setShortcut( tr( "Alt+Ctrl+S" ) );
	screenShotAct->setIcon( QIcon( "resources/save.png" ) );
	screenShotAct->setEnabled( false );
	connect(screenShotAct, SIGNAL(triggered()), this, SLOT(saveScreenshot()));

	saveAct = new QAction( "Save Configuration", this );
	saveAct->setShortcut( tr( "Ctrl+S" ) );
	saveAct->setIcon( screenShotAct->icon() );
	connect(saveAct, SIGNAL(triggered()), this, SLOT(saveConfig()));

	openAct = new QAction( "Open Configuration", this );
	openAct->setShortcut( tr( "Ctrl+O" ) );
	openAct->setIcon( QIcon("resources/open.png") );
	connect(openAct, SIGNAL(triggered()), this, SLOT(openConfig()));

}

void ControlWindow::createMenus ( ) {
	//TODO
	createActions( );
	menuBar = new QMenuBar( this );
	fileMenu = new QMenu(tr("&File"), menuBar );
	fileMenu->addAction(openAct);
	fileMenu->addAction(saveAct);
	fileMenu->addAction(screenShotAct);
	fileMenu->addSeparator();
	fileMenu->addAction(exitAct);

	//viewMenu = new QMenu(tr("&View"), this);
	//viewMenu->addAction(zoomInAct);
	//viewMenu->addAction(zoomOutAct);
	//viewMenu->addAction(normalSizeAct);
	//viewMenu->addSeparator();
	//viewMenu->addAction(fitToWindowAct);

	helpMenu = new QMenu(tr("&Help"), menuBar );
	helpMenu->addAction(aboutAct);
	helpMenu->addAction(aboutQtAct);

	menuBar->addMenu(fileMenu);
	//menuBar->addMenu(viewMenu);
	menuBar->addMenu(helpMenu);
}

void ControlWindow::createControlBox ( ) {
	
	buttonsBox = new QGroupBox( "Controls", this );
	currentButton = new QPushButton( tr( "&Current" ), buttonsBox );
	startButton = new QPushButton( tr( "&Start" ), buttonsBox );
	resetButton = new QPushButton( tr( "&Reset" ), buttonsBox );
	defaultButton = new QPushButton( tr( "&Default" ), buttonsBox );
	startButton->setToolTip( "Press to start/resume/stop rendering" );
	defaultButton->setToolTip( "Press to set deafult rendering values" );
	resetButton->setToolTip( "Press to cancel the rendered image" );
	currentButton->setToolTip( "Press to set current rendering values" );
	QFont f;
	f.setBold( true );
	startButton->setFont( f );
	
	mouseLabel = new QLabel( "Mouse zoom mode:", buttonsBox );
	normalZoom = new QRadioButton( "On center", buttonsBox );
	mouseZoom = new QRadioButton( "On cursor", buttonsBox );
	QVBoxLayout *vbox = new QVBoxLayout ( );
	QHBoxLayout* radioLayout = new QHBoxLayout( );
	QHBoxLayout *buttonsLayout = new QHBoxLayout ( );
	radioLayout->addWidget( normalZoom );
	radioLayout->addWidget( mouseZoom );

	buttonsLayout->addWidget( currentButton );
	buttonsLayout->addWidget( resetButton );

	vbox->addLayout( buttonsLayout );
	vbox->addWidget( defaultButton );
	vbox->addWidget( startButton );
	//vbox->addStretch(1);
	vbox->addWidget( mouseLabel );
	vbox->addLayout( radioLayout );
	buttonsBox->setLayout( vbox );

	
	mouseZoom->setChecked( true );
	normalZoom->setChecked( false );
}










//// BUTTON HANDLING
// I agree that this part is not only a little bit confusing from the point of view of the user
// but it is also very messy from the point of view of the programmer. I think it would be
// better to think at a completely new (better) way to handle the controls, the buttons etc..

void ControlWindow::handleStartButton ( ) {
	_print( "handleStartButton()" );	
	
	b->lock( );
	if ( valuesChanged( ) )
		b->set( cre, cim, scale, highr, highg, highb, renderWin->size() );
		
		
	if ( b->isRunning() ) {
		b->setStatus( RUNNING );
		b->restarted.wakeOne( );
	} else	b->start( );
	b->unlock( );
	
	
	
	
	red = redSlider->value();
	green = greenSlider->value();
	blue = blueSlider->value();
	screenShotAct->setEnabled( true );
	
	if ( renderWin->isHidden() ) 
		renderWin->show();
	
	// visualize the right text on the button depending on the state of the program
	viewStartButton( );
}



void ControlWindow::handleCurrentButton ( ) {
	_print( "handleCurrentButton()" );
	// set the current values
	setColorSliders( red, green, blue );
	setImageSliders( loadedLightness, loadedContrast, fps );
	
	if ( !b->isRunning() )
		setValues( initialCre, initialCim, initialScale );
	else
		setValues( b->cre, b->cim, b->scale );
}

void ControlWindow::handleResetButton ( ) {
	_print( "handleResetButton()" );
	b->lock();
	b->setStatus( PAUSED );
	b->stopped.wait( &(b->mutex) );
	
	b->clear();
	b->setStatus( RUNNING );
	b->restarted.wakeOne();
	b->unlock();
	
	renderWin->repaint();
	renderWin->clearBuffers();
}

void ControlWindow::viewStartButton ( ) {
	//_print( "viewStartButton()" );
	
	b->lock();
	if ( valuesChanged() || b->cleaned )
			setButtonStart( );
	else 	setButtonResume( );
	b->unlock();
}

void ControlWindow::handleDefaultButton ( ) {
	//_print( "handleDefaultButton()" );
	setColorSliders( initialRed, initialGreen, initialBlue );
	setValues( initialCre, initialCim, initialScale );
	setImageSliders( initialLight, initialContrast, initialFps );
	
	//updateValues( );
	//viewStartButton ( );
}

bool ControlWindow::valuesChanged ( ) {
	//_print( "valuesChanged()" );
	
	return  cre != b->cre || cim != b->cim || scale != b->scale ||
		highr != b->highr || highg != b->highg || highb != b->highb ||
		renderWin->valuesChanged();
}


void ControlWindow::setColorSliders ( int r, int g, int b ) {
	//_print( "setColorSliders()" );
	redSlider->setSliderPosition( r );
	greenSlider->setSliderPosition( g );
	blueSlider->setSliderPosition( b );
}

void ControlWindow::setImageSliders ( int l, int c, int f ) {
	lightSlider->setValue( l );
	contrastSlider->setValue( c );
	fpsSlider->setValue( f );
}

void ControlWindow::setValues ( double cre, double cim, double scale ) {
	reBox->setValue( cre );
	imBox->setValue( cim );
	zoomBox->setValue( scale );
	
	//this->cre = cre;
	//this->cim = cim;
	//this->scale = scale;
	setCre( cre );
	setCim( cim );
	setScale( scale );
	viewStartButton();
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

void ControlWindow::setRedIterationDepth ( int value ) {
	//_print( "setRedIterationDepth()" );
	highr = (int) pow( 2.0, value / 2.0 );
	updateRedLabel( );
	viewStartButton ( );
}

void ControlWindow::setGreenIterationDepth ( int value ) {
	//_print( "setGreenIterationDepth()" );
	highg = (int) pow( 2.0, value / 2.0 );
	updateGreenLabel( );
	viewStartButton ( );
}

void ControlWindow::setBlueIterationDepth ( int value ) {
	//_print( "setBlueIterationDepth()" );
	highb = (int) pow( 2.0, value / 2.0 );
	updateBlueLabel( );
	viewStartButton ( );
}

void ControlWindow::setLightness ( int value ) {
	//_print( "setLightness()" );
	lightness = value;
	//b->setLightness( (double) value / ( lightSlider->maximum() - value + 1 ) );
	//printf( "Lightness: %d %lf\n", value,(double) value / ( lightSlider->maximum() - value ) );
	b->setLightness( value );
}

void ControlWindow::setContrast ( int value ) {
	//_print( "setContrast()" );
	contrast = value;
	// ottengo un valore fra 0.0 e 2.0
	//b->setContrast( (double) value / contrastSlider->maximum() * 2.0 );
	b->setContrast( value );
}

void ControlWindow::setFps ( int value ) {
	fps = value;
	float toSet = ( fps == 0 ) ? 0.0 : fps / 10.0;
	b->setFps( toSet );
	updateFpsLabel( );
}

void ControlWindow::setCre ( double d ) {
	//_print( "setCre()" );
	cre = d;
	if ( cre < minRe ) cre = minRe;
	if ( cre > maxRe ) cre = maxRe;
	viewStartButton ( );
}

void ControlWindow::setCim ( double d ) {
	//_print( "setCim()" );
	cim = d;
	if ( cim < minIm ) cim = minIm;
	if ( cim > maxIm ) cim = maxIm;
	viewStartButton ( );
}

void ControlWindow::setScale ( double d ) {
	//_print( "setScale()" );
	scale = d;
	
	if ( scale < minScale ) scale = minScale;
	if ( scale > maxScale ) scale = maxScale;
	step = 10 / scale;
	imBox->setSingleStep( step );
	reBox->setSingleStep( step );
	viewStartButton ( );
}
















// UTILITY FUNCTIONS


void ControlWindow::renderWinClosed ( ) {
	b->lock();
	b->setStatus( PAUSED );
	b->unlock();
	
	screenShotAct->setEnabled( false );
	viewStartButton( );
}

void ControlWindow::render ( ) {
	b->lock();
	b->setStatus( PAUSED );
	b->stopped.wait( &(b->mutex) );
	
	b->set( cre, cim, scale, highr, highg, highb, renderWin->size() );
	b->setStatus( RUNNING );
	b->restarted.wakeOne();
	b->unlock();
}

void ControlWindow::closeEvent ( QCloseEvent* ) {
	exit( );
}

void ControlWindow::setAlgorithm( bool algo ) {
	b->lock();
	b->setAlgorithm( algo );
	b->unlock();
}

void ControlWindow::exit ( ) {
	renderWin->close();
	b->lock();
	b->setStatus( ABORTED );
	b->restarted.wakeOne( );
	b->unlock();
	_print( "ControlWindow::exit() called." );
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
	QString name = "[" + QString::number( this->cre ) + ", " + QString::number(cim ) + "].png";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Screenshot"), 
			   "./" + name, tr("Image Files (*.png)"));
	renderWin->saveScreenshot( fileName );
}


void ControlWindow::saveConfig ( ) {
	QString name = "[" + QString::number( this->cre ) + ", " + QString::number(cim ) + "].Buddha++";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Current Config"), "./" + name,
			   tr("Buddha++ Files (*.Buddha++)") );
	if ( fileName.isNull() ) return;
	QFile file( fileName );
	file.open(QIODevice::WriteOnly);
	QDataStream out( &file );

	out << red << green << blue << cre << cim << scale << lightness << contrast << renderWin->size();
	file.close();
}


void ControlWindow::openConfig ( ) {
	QString fileName = QFileDialog::getOpenFileName( this, tr("Open"),
			   "./", tr("Buddha++ Files (*.Buddha++)") );

	if ( fileName.isNull() ) return;
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

	
	
	
	this->setValues(r, i, s );
	this->setColorSliders(rr, gg, bb);
	loadedContrast = cc;
	loadedLightness = ll;
	this->setImageSliders( ll, cc, initialFps );
	
	/*if ( ws != renderWin->size() ) {
		if ( !b->cleaned ) {
			int answer = QMessageBox::question( this, "Do you want to reset current rendering?", 
				     "The selected configuration has a different rendering size compared "
				     "with the current values. Changing the size of the render window will erase the "
				     "current calculus. Do you want to continue?", QMessageBox::Yes | QMessageBox::No );
		
			if ( answer == QMessageBox::No ) return;
		}
		
		renderWin->resize( ws );
		renderWin->clearBuffers( );
		renderWin->repaint();
	}*/
}
