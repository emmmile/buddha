#ifndef MANDELBROT_BASE_H
#define MANDELBROT_BASE_H

#include <bitset>
#include <vector>
#include <thread>
#include <iostream>

#include "settings.h"
using namespace std;

template<class C> // complex type
class mandelbrot_base {
public:
	mandelbrot_base( const settings& s ) 
		: s(s) {
	}

	inline bool inside ( C& c ) const {
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
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;

	    for ( unsigned int i = 0; i < s.high; ++i ) {
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
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;

	    for ( unsigned int i = 0; i < s.high; ++i ) {
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

	    calculated = s.high;
	    return -1;
	}


	// this is the main function. Here little modifications impacts a lot on the speed of the program!
	inline int evaluate ( vector<C>& seq, unsigned int& contribute, unsigned int& calculated ) const {
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;
	    contribute = 0;

	    for ( unsigned int i = 0; i < s.high; ++i ) {
	        //cout << i << " " << seq[i] << endl;
	        //getchar();

	        // this checks if the seq[i] point is inside the screen
	        if ( inside( seq[i] ) )
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

	    calculated = s.high;
	    return -1;
	}

	const settings& s;
};

#endif