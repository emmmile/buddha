#ifndef SETTINGS_H
#define SETTINGS_H


#include <complex>
#include <dlfcn.h>
#include <fstream>
#include <string>
#define BOOST_LOG_DYN_LINK
#include <boost/log/trivial.hpp>
using namespace std;


typedef unsigned char uchar;

struct settings {
	// since this class is also used as "container" for the various generators
    // I use directly public variables instead private members and functions like set*()
    // buddhabrot characteristics
    double maxre, maxim;
    double minre, minim;
    double cre, cim;
    uint32_t low;
    uint32_t lowr;
    uint32_t lowg;
    uint32_t lowb;
    uint32_t high;
    uint32_t highr;
    uint32_t highg;
    uint32_t highb;
    double scale;


    // these can be calculated from the previous but they are useful
    double rangere, rangeim;
    uint64_t w;
    uint64_t h;
    uint64_t size;


    uint32_t contrast, lightness;
 	float realContrast, realLightness;

    static const uint32_t maxLightness = 200;
    static const uint32_t maxContrast = 200;



    string outfile;
    string infile;
    uint32_t threads;
    bool inverse;

    string formula;


    void (*next_point)(complex<double>&, complex<double>&);


	void indirect_settings ( );
    void compile_formula ( );
    void dump ( ) const;
};

#endif