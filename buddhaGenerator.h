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


