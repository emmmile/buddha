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
#include "renderWindow.h"
#include "demoWindow.h"
#include <QVBoxLayout>
#include <QtGui>
#include <iostream>



ControlWindow::ControlWindow ( ) {
	//timer = new QTimer( this );
	b = new Buddha( );
	icon = new QIcon( "resources/icon.png" );
	setWindowIcon( *icon );

	centralWidget = new QWidget( this );
	createGraphBox();
	createRenderBox();
	createControlBox();
	createMenus();
	timer = new QTimer( this );



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


	connect( timer, SIGNAL( timeout() ), renderWin, SLOT( sendFrameRequest() ) );
        connect(minRbox, SIGNAL(valueChanged(int)), this, SLOT(setMinRIteration(int)));
        connect(minGbox, SIGNAL(valueChanged(int)), this, SLOT(setMinGIteration(int)));
        connect(minBbox, SIGNAL(valueChanged(int)), this, SLOT(setMinBIteration(int)));
        connect(maxRbox, SIGNAL(valueChanged(int)), this, SLOT(setMaxRIteration(int)));
        connect(maxGbox, SIGNAL(valueChanged(int)), this, SLOT(setMaxGIteration(int)));
        connect(maxBbox, SIGNAL(valueChanged(int)), this, SLOT(setMaxBIteration(int)));

	connect( reBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCre( double ) ) );
	connect( imBox, SIGNAL( valueChanged( double ) ), this, SLOT( setCim( double ) ) );
	connect( zoomBox, SIGNAL( valueChanged( double ) ), this, SLOT( setScale( double ) ) );

	connect( lightSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setLightness( int ) ) );
	connect( contrastSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setContrast( int ) ) );
	connect( fpsSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setFps( int ) ) );
	connect( threadsSlider, SIGNAL( valueChanged( int ) ), this, SLOT( setThreadNum( int ) ) );
	connect( startButton, SIGNAL( clicked() ), this, SLOT(handleStartButton()));
	connect( resetButton, SIGNAL( clicked() ), this, SLOT( handleResetButton() ) );
#if DEMO_WINDOW
	demoWin = new DemoWindow( this, b );
	//demoWin->show();
	connect( demoButton, SIGNAL( clicked() ), this, SLOT( handleDemoButton() ) );
	connect( timer, SIGNAL( timeout() ), demoWin, SLOT( sendFrameRequest() ) );
#endif
	connect( normalZoom, SIGNAL( toggled( bool) ), renderWin, SLOT( setMouseMode( bool ) ) );
	
	// i set some shortcuts also for renderWin
	connect( new QShortcut( startButton->shortcut(), renderWin ), SIGNAL(activated()), startButton, SLOT(animateClick()) );
	connect( new QShortcut( resetButton->shortcut(), renderWin ), SIGNAL(activated()), resetButton, SLOT(animateClick()) );
	connect( new QShortcut( screenShotAct->shortcut(), renderWin ), SIGNAL(activated()), this, SLOT(saveScreenshot()) );
	
        connect( this, SIGNAL( setValues( double, double, double, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, QSize, bool ) ),
                 b, SLOT( set( double, double, double, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, QSize, bool ) ) );

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
        connect( minRbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
        connect( minGbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
        connect( minBbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
        connect( maxRbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
        connect( maxGbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );
        connect( maxBbox, SIGNAL( valueChanged( int ) ), this, SLOT( sendValues( ) ) );

}



void ControlWindow::createGraphBox ( ) {
	graphBox = new QGroupBox( tr( "Graph quality" ), this );
	
	iterationRedLabel = new QLabel( "Red iteration min/max:", graphBox );
        minRbox = new QSpinBox(graphBox);
        minRbox->setMinimum(0);
        minRbox->setMaximum(INT_MAX);
        minRbox->setAlignment(Qt::AlignCenter);
        minRbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	minRbox->setValue(lowr);

        maxRbox = new QSpinBox(graphBox);
        maxRbox->setMinimum(0);
        maxRbox->setMaximum(INT_MAX);
        maxRbox->setAlignment(Qt::AlignCenter);
        maxRbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	maxRbox->setValue(highr);

	iterationGreenLabel = new QLabel( "Green iteration min/max:", graphBox );
        minGbox = new QSpinBox(graphBox);
        minGbox->setMinimum(0);
        minGbox->setMaximum(INT_MAX);
        minGbox->setAlignment(Qt::AlignCenter);
        minGbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	minGbox->setValue(lowg);

        maxGbox = new QSpinBox(graphBox);
        maxGbox->setMinimum(0);
        maxGbox->setMaximum(INT_MAX);
        maxGbox->setAlignment(Qt::AlignCenter);
        maxGbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	maxGbox->setValue(highg);

	iterationBlueLabel = new QLabel( "Blue iteration min/max:", graphBox );
        minBbox = new QSpinBox(graphBox);
        minBbox->setMinimum(0);
        minBbox->setMaximum(INT_MAX);
        minBbox->setAlignment(Qt::AlignCenter);
        minBbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	minBbox->setValue(lowb);

        maxBbox = new QSpinBox(graphBox);
        maxBbox->setMinimum(0);
        maxBbox->setMaximum(INT_MAX);
        maxBbox->setAlignment(Qt::AlignCenter);
        maxBbox->setButtonSymbols( QAbstractSpinBox::PlusMinus );
	maxBbox->setValue(highb);


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
        QHBoxLayout *rHLayout = new QHBoxLayout( );
        QHBoxLayout *gHLayout = new QHBoxLayout( );
        QHBoxLayout *bHLayout = new QHBoxLayout( );

	
	
	vbox->addWidget( iterationRedLabel );
        rHLayout->addWidget(minRbox);
        rHLayout->addWidget(maxRbox);
        vbox->addLayout(rHLayout);

	vbox->addWidget( iterationGreenLabel );
        gHLayout->addWidget(minGbox);
        gHLayout->addWidget(maxGbox);
        vbox->addLayout(gHLayout);

	vbox->addWidget( iterationBlueLabel );
        bHLayout->addWidget(minBbox);
        bHLayout->addWidget(maxBbox);
        vbox->addLayout(bHLayout);

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

	//vbox->addLayout( buttonsLayout );
	vbox->addWidget( mouseLabel );
	vbox->addLayout( radioLayout );
	vbox->addWidget( resetButton );
	vbox->addWidget( startButton );
#if DEMO_WINDOW
	demoButton = new QPushButton( tr( "&Demo" ), buttonsBox );
	startButton->setToolTip( "Press to open demo window" );
	demoButton->setDisabled( true );
	vbox->addWidget( demoButton );
	connect( b, SIGNAL( stoppedGenerators( bool ) ), demoButton, SLOT( setDisabled( bool ) ) );
	connect( b, SIGNAL( startedGenerators( bool ) ), demoButton, SLOT( setEnabled( bool ) ) );
#endif
	//vbox->addStretch(1);
	buttonsBox->setLayout( vbox );


	
	mouseZoom->setChecked( true );
	normalZoom->setChecked( false );


	connect( b, SIGNAL( stoppedGenerators( bool ) ), startButton, SLOT( setEnabled( bool ) ) );
	connect( b, SIGNAL( stoppedGenerators( bool ) ), resetButton, SLOT( setDisabled( bool ) ) );
	connect( b, SIGNAL( startedGenerators( bool ) ), resetButton, SLOT( setEnabled( bool ) ) );
	connect( b, SIGNAL( startedGenerators( bool ) ), startButton, SLOT( setDisabled( bool ) ) );
}


void ControlWindow::sendValues ( bool pause ) {
	if ( this->valuesChanged() ) {

		emit setValues( cre, cim, scale, lowr, lowg, lowb, highr, highg, highb, renderWin->size(), pause );
	}
}




//// BUTTON HANDLING
void ControlWindow::handleStartButton ( ) {
	qDebug() << "ControlWindow::handleStartButton(), thread " << QThread::currentThreadId();
	

#if DEMO_WINDOW
	b->setDemo( 0.0, 0.0, 200.0, demoWin->size(), true );
#endif
	emit sendValues( false );
	emit startCalculation( );
	timer->start( sleepTime );

        screenShotAct->setEnabled( true );
	
	if ( renderWin->isHidden() ) 
		renderWin->show();
}


void ControlWindow::handleResetButton ( ) {
	renderWin->repaint();
	//renderWin->clearBuffers();
	emit clearBuffers( );
}

#if DEMO_WINDOW
void ControlWindow::handleDemoButton() {
	qDebug() << "ControlWindow::handleStartButton(), thread " << QThread::currentThreadId();


#if DEMO_WINDOW
	b->setDemo( 0.0, 0.0, 200.0, demoWin->size(), true );
#endif


	if ( demoWin->isHidden() )
		demoWin->show();
	else	demoWin->hide();
}
#endif

bool ControlWindow::valuesChanged ( ) {
	return  cre != b->cre || cim != b->cim || scale != b->scale ||
                highr != b->highr || highg != b->highg || highb != b->highb ||
                lowr != b->lowr || lowg != b->lowg || lowb != b->lowb ||
		renderWin->width() != (int) b->w || renderWin->height() != (int) b->h;
}


void ControlWindow::modelToGUI ( ) {
	minRbox->setValue( lowr );
	minGbox->setValue( lowg );
	minBbox->setValue( lowb );
	maxRbox->setValue( highr );
	maxGbox->setValue( highg );
	maxBbox->setValue( highb );

	lightSlider->setValue( lightness );
	contrastSlider->setValue( contrast );
	fpsSlider->setValue( fps * 10.0 );
	//setFps( fps );
	//setLightness( lightness );
	//setContrast( contrast );

	reBox->setValue( cre );
	imBox->setValue( cim );
	zoomBox->setValue( scale );

	// why?
	setCre( cre );
	setCim( cim );
	setScale( scale );
}











// FUNCTIONS FOR THE INPUT WIDGETS

void ControlWindow::updateFpsLabel( ) {
	fpsLabel->setText( "Frames per second: [" + QString::number( fps, 'f', 1 ) + "]" );
}

void ControlWindow::updateThreadLabel( quint8 value ) {
	threadsLabel->setText( "Threads: [" + QString::number( value ) + "]" );
}

void ControlWindow::setMinRIteration(int value) {
    lowr = value;
}

void ControlWindow::setMinGIteration(int value) {
    lowg = value;
}

void ControlWindow::setMinBIteration(int value) {
    lowb = value;
}

void ControlWindow::setMaxRIteration(int value) {
    highr = value;
}

void ControlWindow::setMaxGIteration(int value) {
    highg = value;
}

void ControlWindow::setMaxBIteration(int value) {
    highb = value;
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
	fps = ( ( value == 0 ) ? 0.0 : value / 10.0 );
	sleepTime = (fps == 0.0) ? 0x0FFFFFFF : 1000.0f / fps;
	updateFpsLabel( );
	
	if ( timer->isActive() ) {
		timer->stop();
		timer->start( sleepTime );
	}
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
	//step = 10 / scale;
	//imBox->setSingleStep( step );
	//reBox->setSingleStep( step );
	//viewStartButton ( );
}

void ControlWindow::setThreadNum ( int value ) {
	updateThreadLabel( value );
	emit changeThreadNumber( value );
}



void ControlWindow::showEvent( QShowEvent* ) {
	modelToGUI( );
}










// UTILITY FUNCTIONS


void ControlWindow::renderWinClosed ( ) {	
	screenShotAct->setEnabled( false );
	timer->stop( );
	
	// here an asynchronous termination is sufficient
	emit stopCalculation( );
	emit clearBuffers( );

#if DEMO_WINDOW
	demoWin->close();
#endif
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
		    "with flags \"<b>" XSTR(FLAGS) "\"</b>.</p>" ) );
}



void ControlWindow::saveScreenshot ( ) {
	// simply opens a dialog and send a save request to the buddha thread
	QString name = "[" + QString::number( cre ) + ", " + QString::number( cim ) + "].png";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Screenshot"), 
			   "./" + name, tr("Image Files (*.png)"));
			   
	emit screenshotRequest( fileName );
}






// TODO these functions are completely to review
void ControlWindow::saveConfig ( ) {
	QString name = "[" + QString::number( this->cre ) + ", " + QString::number(cim ) + "].xml";
	QString fileName = QFileDialog::getSaveFileName( this, tr("Save Current Config"), "./" + name,
			   tr("Buddha++ Files (*.xml)") );

	this->options->save( fileName.toStdString() );
}


void ControlWindow::openConfig ( ) {
	QString fileName = QFileDialog::getOpenFileName( this, tr("Open"),
			   "./", tr("Buddha++ Files (*.xml)") );


}
