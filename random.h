#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

#define USE_BOOST		1

#if USE_BOOST
#include <boost/random.hpp>
#endif

// RAND_MAX on windows is 0x7FFF
#ifdef _WIN32
#undef RAND_MAX
#define RAND_MAX 0x7FFFFFFF
#endif

class Random {
private:
#if USE_BOOST
	// the best are mt19937, taus88, rand48 (in order of goodness-slowness)
	typedef boost::rand48 random_generator;
	random_generator generator;

	inline int32_t gen ( ) {
		return generator() & RAND_MAX;
	}
#else
	uint32_t x, y, z, w;

	inline int32_t gen ( ) {
		uint32_t t;

		t = x ^ (x << 11);
		x = y; y = z; z = w;
		return ( w = w ^ (w >> 19) ^ (t ^ (t >> 8)) ) & RAND_MAX;
	}
#endif
public:
	Random ( uint32_t seed = 123456789 ) {
#if USE_BOOST
		generator.seed( seed );
#else
		x = seed;
		y = 362436069;
		z = 521288629;
		w = 88675123;
#endif
	}

	// get uniformly an integer in [0,RAND_MAX)
	int32_t integer ( ) {
		return gen();
	}

	// get uniformly a real in [0,1)
	double real ( ) {
		return gen() / (double) RAND_MAX;
	}

	// get uniformly a real in (-1,1)
	double realnegative ( ) {
		return ( gen() << 1 ) / (double) RAND_MAX;
	}

	// get uniformly a real in [0,2)
	double real2 ( ) {
		return gen() / RAND_MAX * 2.0;
	}

	// get uniformly a real in (-2,2)
	double real2negative ( ) {
		return ( gen() << 1 ) / RAND_MAX * 2.0;
	}

	// get uniformly a real in the unit disk
	double realdisk ( double& x, double& y ) {
		// this is usually a lot faster than using sin, cos and sqrt (below),
		// since the probability that a point is accepted is high: pi/4
		double s;
		while ( 1 ) {
			x = realnegative();
			y = realnegative();

			s = x * x + y * y;
			if ( s <= 1.0 ) return s;
		}

		// all generators except mt19937 can generate artifacts with the following
		// procedure that choose an angle in [0,2pi) and a radius in [0,1) and then
		// project the point on x and y. I don't know why.
		// http://mathworld.wolfram.com/DiskPointPicking.html
		/*double phi = M_PI * real2();
		double r = sqrt( real() );
		x = r * cos( phi );
		y = r * sin( phi );*/
	}

	// generate a two dimension random point normally distributed (mean = 0, variance = 1)
	void gaussian ( double& x, double& y ) {
		// this is the exact one (Marsaglia polar method applied to Box-Muller transform)
		double s = realdisk( x, y );
		double factor = sqrt( -2.0 * log( s ) / s );
		x = x * factor;
		y = y * factor;

		// this is approximated and uses the central limit thorem
		/*int64_t tmp = 0;
		for ( int i = 0; i < 12; ++i ) tmp += gen();
		x = tmp / (double) RAND_MAX - 6.0; tmp = 0;
		for ( int i = 0; i < 12; ++i ) tmp += gen();
		y = tmp / (double) RAND_MAX - 6.0;*/

	}

	void gaussian ( double& x, double& y, double radius ) {
		double s = realdisk( x, y );
		double factor = sqrt( -2.0 * log( s ) / s ) * radius;
		x = x * factor;
		y = y * factor;
	}

	// generate a two dimension random point exponentially distributed
	// (actually this is not really exponential i think but hower is much more
	// dense aroun the origin than the gaussian mutation)
	void exponential ( double& x, double& y ) {
		double s = realdisk( x, y );
		double factor = -log( s ) / s;
		x = x * factor;
		y = y * factor;
	}

	void exponential ( double& x, double& y, double radius ) {
		double s = realdisk( x, y );
		double factor = -log( s ) / s * radius;
		x = x * factor;
		y = y * factor;
	}

	// change the seed
	void seed ( uint32_t seed ) {
#if USE_BOOST
		generator.seed( seed );
#else
		x = seed;
#endif
	}
};


#endif // RANDOM_H
