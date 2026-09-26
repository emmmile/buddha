#ifndef MANDELBROT_BASE_H
#define MANDELBROT_BASE_H

#include <bitset>
#include <vector>
#include <thread>
#include <iostream>

#include "buddha_kernel.h"
#include "settings.h"
using namespace std;

template <class C> // complex type
class mandelbrot_base {
  public:
    mandelbrot_base(const settings &s) : s(s) {}

    inline bool inside(C &c) const {
        return c.real() <= s.maxre && c.real() >= s.minre &&
               ((c.imag() <= s.maxim && c.imag() >= s.minim) ||
                (s.symmetric_image && -c.imag() <= s.maxim && -c.imag() >= s.minim));
    }

    // Escape and periodicity rules are shared with buddha-metal (buddha_kernel.h). Each evaluate
    // returns the last orbit index before escape, or -1 for periodic and capped orbits.
    typedef typename C::value_type real_type;
    typedef buddha_kernel::periodicity<real_type> periodicity;

    inline int evaluate(vector<C> &seq) const {
        periodicity period;
        period.reset();

        for (unsigned int i = 0; i < s.high; ++i) {
            // test the stop condition and eventually continue a little bit
            if (buddha_kernel::escaped(norm(seq[i])))
                return i - 1;

            if (period.cyclic(seq[i].real(), seq[i].imag(), i))
                return -1;

            // next_point( seq[i], begin );
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }

        return -1;
    }

    inline int evaluate(vector<C> &seq, unsigned int &calculated) const {
        periodicity period;
        period.reset();

        for (unsigned int i = 0; i < s.high; ++i) {
            // test the stop condition and eventually continue a little bit
            if (buddha_kernel::escaped(norm(seq[i]))) {
                calculated = i;
                return i - 1;
            }

            if (period.cyclic(seq[i].real(), seq[i].imag(), i)) {
                calculated = i;
                return -1;
            }

            // next_point( seq[i], begin );
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }

        calculated = s.high;
        return -1;
    }

    // this is the main function. Here little modifications impacts a lot on the speed of the
    // program!
    inline int evaluate(vector<C> &seq, unsigned int &contribute, unsigned int &calculated) const {
        periodicity period;
        period.reset();
        contribute = 0;

        for (unsigned int i = 0; i < s.high; ++i) {
            // cout << i << " " << seq[i] << endl;
            // getchar();

            // this checks if the seq[i] point is inside the screen
            if (inside(seq[i]))
                ++contribute;

            // test the stop condition and eventually continue a little bit
            if (buddha_kernel::escaped(norm(seq[i]))) {
                calculated = i;
                return i - 1;
            }

            if (period.cyclic(seq[i].real(), seq[i].imag(), i)) {
                calculated = i;
                return -1;
            }

            // next_point( seq[i], begin );
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }

        calculated = s.high;
        return -1;
    }

    const settings &s;
};

#endif
