

#ifndef BuddhaGenerator_H
#define BuddhaGenerator_H

#include <string>
#include <vector>
#include <cmath>
#include <cfloat>
#include <stdlib.h>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <cstdio>
#include <iostream>
#include "buddha.h"
#include "staticutils.h"
using namespace std;





class BuddhaGenerator : public QThread {
public:	
	Buddha* b;

	// for the raw image and the sequence of points
	vector<complex> seq;
	unsigned int* raw;
	
	// things for the random stuff
	struct random_data buf;
	char statebuf [256];
	unsigned long int seed;
	
	
	// for the synchronization
	QMutex wmutex;
	QWaitCondition wcondition;
	CurrentStatus status;
	MemoryStatus memory;
	
	// utily functions
	void initialize ( Buddha* b );
	void setStatus ( CurrentStatus s );
	
	
	// calculus
	void drawPoint ( complex& c, bool r, bool g, bool b );
	int inside ( complex& c );
	int evaluate ( unsigned int& calculated );
	int findPoint ( unsigned int& calculated );
	int test ( unsigned int& calculated );
	double distance ( unsigned int slen );
	
	unsigned int contribute(int);
	int normal();
	int metropolis();
	
	// this is the anti-buddhabrot evaluate function
	// it would be nice to put a flag somewhere and have the possibility
	// somewhere to choose from the interface if render the buddhabrot or
	// the antibuddhabrot.
	int anti ( unsigned int& calculated );
	
	void run ( );
	void handleMemory( );
	bool flowControl ( );
};

#endif


