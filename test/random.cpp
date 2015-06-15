
#include <complex>
#include <random>
#include "random.h"
#include "timer.h"

#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>


template<class T, class U, class G>
void testBuiltin ( T& distribution, U& uniform, G& generator, unsigned int n ) {
	timer time;

	complex<double> sum(0,0);
	for ( unsigned int i = 0; i < n; ++i ) {
		sum += polar( fabs(distribution(generator)), uniform(generator) );
	}

	BOOST_LOG_TRIVIAL(info) << sum;
    BOOST_LOG_TRIVIAL(info) << "normal_distribution: " << time.elapsed() << " s";
}

template<class T>
void testRandom ( T& r, unsigned int n ) {
	timer time;

	complex<double> sum(0,0);
	for ( unsigned int i = 0; i < n; ++i ) {
		double x;
		double y;
		r.gaussian(x, y, 2.0);
		sum += complex<double>(x, y);
	}

	BOOST_LOG_TRIVIAL(info) << sum;
    BOOST_LOG_TRIVIAL(info) << "Random: " << time.elapsed() << " s";
}


int main ( int argc, char** argv ) {
	unsigned int n = 50000000;
	
    std::random_device rd;
    std::mt19937 gen(rd());

    std::normal_distribution<> gaussian(0,2);
	std::uniform_real_distribution<double> uniform(0.0,2 * M_PI);

	RandomBase<mt19937> r(rd());

    testBuiltin( gaussian, uniform, gen, n );
    testRandom( r, n );
}