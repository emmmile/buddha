// This is a template for the file generated for the formula compilation.
// I made some simple benchmarking and I found that dynamically loading the formula
// produces these results (calling just the "evaluate()" function, where this one
// is actually called):
//   original: ~180 Mpoints/s
//   dynamic:  ~125 Mpoints/s
// I think the difference is due to the impossibility to inline the code in the 
// dynamic case.


#include <complex.h>
using namespace std;
extern "C" void foo ( complex<double>& z, complex<double>& c ) {
  // formula goes here
}
