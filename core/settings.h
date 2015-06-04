#ifndef SETTINGS_H
#define SETTINGS_H


#include <complex>
#include <dlfcn.h>
#include <fstream>
#include <string>
#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>
using namespace std;

struct settings {
	// since this class is also used as "container" for the various generators
    // I use directly public variables instead private members and functions like set*()
    // buddhabrot characteristics
    double maxre, maxim;
    double minre, minim;
    double cre, cim;
    uint low;
    uint lowr;
    uint lowg;
    uint lowb;
    uint high;
    uint highr;
    uint highg;
    uint highb;
    double scale;


    // these can be calculated from the previous but they are useful
    double rangere, rangeim;
    uint w;
    uint h;
    uint size;


    uint contrast, lightness;
 	float realContrast, realLightness;

    static const uint maxLightness = 200;
    static const uint maxContrast = 200;



    string outfile;
    string infile;
    uint threads;
    bool inverse;

    string formula;


    void (*next_point)(complex<double>&, complex<double>&);


	void indirect_settings ( );
    void compile_formula ( );
    void dump ( ) const;
};

#endif