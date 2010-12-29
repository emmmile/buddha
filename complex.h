
#ifndef COMPLEX_H
#define COMPLEX_H

#include <iostream>

/// semplified structure to store complex numbers and generate random complex numbers
class complex {
public:
	double re;
	double im;

	
	complex( double re, double im ) {
		this->re = re;
		this->im = im;
	}	
	
	complex( ) {
		//complex( 0.0, 0.0 );
	}
	
	inline complex& operator = ( const complex& z ) {
		this->re = z.re;
		this->im = z.im;
		return *this;
	}
	/*
	friend complex& operator -= ( complex& z, const complex& t ) {
		z.re -= t.re;
		z.im -= t.im;
		return z;
	}
	
	friend complex& operator += ( complex& z, const complex& t ) {
		z.re += t.re;
		z.im += t.im;
		return z;
	}*/
	
	friend const complex operator - (const complex& z, const complex& t ) {
		return complex( z.re - t.re, z.im - t.im );
	}
	
	friend const complex operator + (const complex& z, const complex& t ) {
		return complex( z.re + t.re, z.im + t.im );
	}
	
	inline void randomGaussian2 ( struct random_data* buf ) {
		re = -0.3333333333333333;
		im = 0.0;
		mutate ( 1.0, buf );
	}
	
	inline void random2 ( struct random_data* buf ) {
		int32_t result;
		
		random_r( buf, &result );
		re = ( result << 1 ) * 9.31322575049159384821E-10;
		random_r( buf, &result );
		im = ( result << 1 ) * 9.31322575049159384821E-10;
	}
	
	inline double mod ( ) {
		return re * re + im * im;
	}
	
	
	inline double randomCircle ( struct random_data* buf ) {
		int32_t result;	
		
		while ( TRUE ) {
			random_r( buf, &result );
			re = ( result << 1 ) * 4.656612875245796924105E-10;
			random_r( buf, &result );
			im = ( result << 1 ) * 4.656612875245796924105E-10;
	
			double s = mod( );
			if ( s < 1.0 ) return s;
		}
	}
	
	
	void mutate ( double radius, struct random_data* buf ) {
		// XXX can be optimized I think
		// the Marsaglia polar method
		complex cc;
		double s = cc.randomCircle( buf );
		double factor = sqrt( -2.0 * log( s ) / s ) * radius;
		re += cc.re * factor;
		im += cc.im * factor;
	}
};


#endif
