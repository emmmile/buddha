

#include <unistd.h>
#include <sys/time.h>
#include "staticutils.h"
#include <cmath>

// measure the time between two calls. Not thread safe!!!
// Use a parameter instead static variables to make it safe.
double ttime ( void ) {
	static struct timeval t;
	static double old = 0.0, newval = 0.0;
	
	gettimeofday ( &t, NULL );
	
	old = newval;
	newval = t.tv_sec + t.tv_usec * 0.000001;
	return newval - old;
}


