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


#ifndef CONTROLWINDOW_H
#define CONTROLWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QtGui>
#include "renderWindow.h"
#include "buddha.h"

#define PRECISION	15










class ControlWindow : public QMainWindow {
	Q_OBJECT

public:
	unsigned int highr, highg, highb;
	int red, green, blue;
	int contrast, lightness, fps;
	int loadedContrast, loadedLightness;
	double cre;
	double cim;
	double scale;
	double step;
	Buddha* b;
	
	static const int initialRed = 26;
	static const int initialGreen = 20;
	static const int initialBlue = 14;
	static const int initialLight = 100;
	static const int initialContrast = 100;
	static const double initialScale = 200;
	static const double initialCre = 0.0;
	static const double initialCim = 0.0;
	static const int initialFps = 20;
	
	static const double minScale = 100.0;
	static const double maxScale = 1.34217728E+8 * 128.0;
	static const double maxRe = 2.0;
	static const double maxIm = 2.0;
	static const double minRe = -2.0;
	static const double minIm = -2.0;
	static const int maxLightness = 200;
	static const int maxContrast = 200;
	static const int maxFps = 40;
	
	// XXX test values
	//cre = -1.009338378906250; cim = -0.907791137695312; scale = 131072;
	//cre = -1.267493247985840; cim = -0.355451583862305; scale = 1.04858e+06;
	//cre = -1.267614841461182; cim = -0.355297327041626; scale = 4.1943e+06;
	//cre = -1.484510898590088; cim = 0.0; scale = 2.09715e+06;
	//cre = -1.262351593017578; cim = -0.408171241760254; scale = 1.04858e+06;
	//cre = 1.021250465869904; cim = 0.0; scale = 16384;
	//cre = -1.211546160731565; cim = -0.703227713802065; scale = 16384;
	//cre = -1.267664853065472; cim = -0.355240946698251; scale = 4.29496e+09;
	// per gli artefatti
	//cre = -1.991965715937304; cim = 0.0; scale = 8192.0;
	
	QWidget *centralWidget;
	QGroupBox *graphBox;
	QGroupBox *buttonsBox;
	QLabel *iterationRedLabel;
	QLabel *reLabel;
	QDoubleSpinBox *reBox;
	QLabel *imLabel;
	QDoubleSpinBox *imBox;
	QLabel *zoomLabel;
	QDoubleSpinBox *zoomBox;
	QLabel *iterationGreenLabel;
	QLabel *iterationBlueLabel;
	QSlider *redSlider;
	QSlider *greenSlider;
	QSlider *blueSlider;
	QGroupBox *renderBox;
	QLabel *contrastLabel;
	QSlider *contrastSlider;
	QLabel *lightLabel;
	QSlider *lightSlider;
	QLabel *fpsLabel, *threadsLabel;
	QSlider *fpsSlider, *threadsSlider;
	QRadioButton* normalZoom, *mouseZoom;
	QIcon* icon;
	QLabel* mouseLabel;
	
	QMenuBar *menuBar;
	QMenu* fileMenu, *viewMenu, *helpMenu;
	
	RenderWindow* renderWin;
	
	int sleepTime;

	void createGraphBox ( );
	void createRenderBox ( );
	void createControlBox ( );
	void createMenus( );
	void createActions( );
	void updateRedLabel( );
	void updateGreenLabel( );
	void updateBlueLabel( );
	void updateFpsLabel( );
	void updateThreadLabel( int );
	void setColorSliders( int, int, int );
	void setImageSliders( int, int, int );
public:
	//QPushButton *currentButton;
	QPushButton *resetButton;
	QPushButton *startButton;
	//QPushButton *defaultButton;
	QAction* exitAct, *aboutQtAct, *aboutAct, *screenShotAct, *saveAct, *openAct;

	ControlWindow ( );
	
	bool valuesChanged( );
	void putValues( double, double, double );
	static int expVal ( int x ) { return (int) pow( 2.0, x / 2.0 ); }
	void setCenter( double, double );
	//void viewStartButton ( );
	double getCre( ) { return cre; }
	double getCim( ) { return cim; }
	double getScale( ) { return scale; }
public slots:
	void handleStartButton( );
	void handleResetButton( );
	//void handleCurrentButton( );
	void renderWinClosed( );
	//void handleDefaultButton( );
	void exit( );
	void setRedIterationDepth( int value );
	void setGreenIterationDepth( int value );
	void setBlueIterationDepth( int value );
	void setLightness( int value );
	void setContrast( int value );
	void setFps( int value );
	void setButtonStart( ) { startButton->setText( tr( "&Start" ) ); }
	void setButtonResume( ) { startButton->setText( tr( "Re&sume" ) ); }
	void setButtonStop( ) { startButton->setText( tr( "&Stop" ) ); }
	void setCre ( double d );
	void setCim ( double d );
	void setScale ( double d );
	void setThreadNum ( int value );
	void about ( );
	void saveScreenshot( );
	void saveConfig( );
	void openConfig( );
	void sendValues( bool pause = true );
signals:
	void closed ( );
	void setValues( double cre, double cim, double scale, uint r, uint g, uint b, QSize wsize, bool pause );
	void startCalculation( );
	void stopCalculation( );
	void pauseCalculation( );
	void clearBuffers( );
	void changeThreadNumber( int );
	void screenshotRequest ( QString fileName );
protected:
	void closeEvent ( QCloseEvent * event );
};

#endif

