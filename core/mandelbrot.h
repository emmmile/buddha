#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <bitset>
#include <vector>
#include <iostream>

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/png_dynamic_io.hpp>
using namespace boost::gil;

#include "settings.h"
#include "timer.h"
using namespace std;

template<class C, unsigned int N = 1024> // complex type
class mandelbrot {
public:
	struct exclusion {
		bitset<N * N / 2> data;
		exclusion(mandelbrot& core) {
			// compute the exclusion map
			vector<C> seq;
			seq.resize(core.high + 1);

			seq[0]=C{-1.4, 0};
			cout << core.evaluate(seq) << endl;

    		BOOST_LOG_TRIVIAL(debug) << "generating " << N << "x" << N << " exclusion map";

    		timer time;
    		rgb8_image_t img(N, N);
		    rgb8_pixel_t red(255, 255, 255);
		    rgb8_image_t::view_t v = view(img);

			for ( int x = 0; x < N; ++x ) {
				for ( int y = 0; y < N / 2; ++y) {
					int i = y * N + x;
					int j = (N - y - 1) * N + x;//y + 2 * (N/2 -y)
					int xx = (x - int(N) / 2);
					int yy = (y - int(N) / 2);

					seq[0] = C(xx * 4.0 / N, yy * 4.0 / N);
					if ( yy == 0 )
						cout << x << " " << y << " " << yy << " " << xx << " -> " << seq[0] << endl;
					data[i] = core.evaluate(seq) == -1;
					if ( data[i] ) v(x, y) =v(x, N-y-1) = red;
				}
			}

			png_write_view("exclusion.png", const_view(img));
			BOOST_LOG_TRIVIAL(debug) << "generated exclusion map in " << time.elapsed() << " s";
		}
		bool operator()( const C& c) const {
			int xx = c.real() * N / 4.0 + N / 2.0;
			int yy = fabs(c.imag()) * N / 4.0 + N / 2.0;
			if ( xx < 0 || xx >= N || yy < 0 || yy >= N / 2 ) return false;

			return data[yy * N + xx];
		}
	};

	mandelbrot( unsigned int low, unsigned int high ) 
		: low(low), high(high), map(*this) {
	}


	inline bool inside ( C& c, const settings& s ) const {
		return  c.real() <= s.maxre && c.real() >= s.minre &&
            ( ( c.imag() <= s.maxim && c.imag() >= s.minim ) ||
              ( -c.imag() <= s.maxim && -c.imag() >= s.minim ) );
	}


	inline bool cyclic( const vector<C>& seq, const unsigned int& i, unsigned int& critical, unsigned int& criticalStep ) const {
		if ( i > criticalStep ) {
            // compute the distance from the critical point
            double distance = norm( seq[i] - seq[critical] );

            // if I found that two calculated points are very very close I conclude that
            // they are the same point, so the sequence is periodic so we are computing a point
            // in the mandelbrot, so I stop the calculation
            if ( distance < FLT_EPSILON * FLT_EPSILON ) { // maybe also DBL_EPSILON is sufficient
                return true;
            }

            // I don't do this step at every iteration to be more fast, I found that a very good
            // compromise is to use a multiplicative distance between each checkpoint
            if ( i == criticalStep * 2 ) {
                criticalStep *= 2;
                critical = i;
            }
        }

        return false;
	}


	inline int evaluate ( vector<C>& seq ) const {
	    unsigned int criticalStep = 32;
	    unsigned int critical = criticalStep;

	   	if ( map(seq[0]) )
	    	return -1;


	    for ( unsigned int i = 0; i < high; ++i ) {
	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 )
	            return i - 1;

	        if ( cyclic( seq, i, critical, criticalStep) ) 
	            return -1;
	        
	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    return -1;
	}


	inline int evaluate ( vector<C>& seq, unsigned int& calculated ) const {
	    unsigned int criticalStep = 32;
	    unsigned int critical = criticalStep;

	   	if ( map(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }


	    for ( unsigned int i = 0; i < high; ++i ) {
	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 ) {
	            calculated = i;
	            return i - 1;
	        }

	        if ( cyclic( seq, i, critical, criticalStep) ) {
	            calculated = i;
	            return -1;
	        }
	        
	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    calculated = high;
	    return -1;
	}


	// this is the main function. Here little modifications impacts a lot on the speed of the program!
	int evaluate ( vector<C>& seq, unsigned int& contribute, unsigned int& calculated, const settings& s ) const {
	    unsigned int criticalStep = 32;
	    unsigned int critical = criticalStep;
	    contribute = 0;

	    if ( map(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

	    for ( unsigned int i = 0; i < high; ++i ) {
	        //cout << i << " " << seq[i] << endl;
	        //getchar();

	        // this checks if the seq[i] point is inside the screen
	        if ( inside( seq[i], s ) )
	            ++contribute;

	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 ) {
	            calculated = i;
	            return i - 1;
	        }
	        
	        if ( cyclic( seq, i, critical, criticalStep) ) {
	            calculated = i;
	            return -1;
	        }

	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    calculated = high;
	    return -1;
	}
private:

	unsigned int low;
	unsigned int high;
	exclusion map;
};

#endif