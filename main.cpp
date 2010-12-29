
#include <Qt>
#include <QtGui>
#include <iostream>
#include "controlWindow.h"
#include "staticutils.h"






int main ( int argc, char** argv ) {
	/*struct random_data buf;
	char statebuf [256];
	buf.state = (int32_t*) statebuf; // this fixes the segfault
	initstate_r( 321, statebuf, sizeof( statebuf ), &buf );
	complex c;
	
	ttime();
	for ( int i = 0; i < 10000000; i++ ) {
		//c.randomCircle( &buf );
		c.randomCircle2( &buf );
	}
	printf( "%lf %lf\n", c.re, ttime() );*/
	
	
	QApplication app(argc, argv);
	ControlWindow control;
	control.show( );
     	
     	
	return app.exec( );
}



