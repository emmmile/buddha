#ifndef MANDELBROT_BASE_H
#define MANDELBROT_BASE_H

#include <bitset>
#include <vector>
#include <thread>
#include <iostream>

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

    inline bool cyclic(const vector<C> &seq, const unsigned int &i, unsigned int &critical,
                       unsigned int &criticalStep) const {
        if (i > criticalStep) {
            // compute the distance from the critical point
            double distance = norm(seq[i] - seq[critical]);

            // if I found that two calculated points are very very close I conclude that
            // they are the same point, so the sequence is periodic so we are computing a point
            // in the mandelbrot, so I stop the calculation
            if (distance < FLT_EPSILON * FLT_EPSILON) { // maybe also DBL_EPSILON is sufficient
                return true;
            }

            // I don't do this step at every iteration to be more fast, I found that a very good
            // compromise is to use a multiplicative distance between each checkpoint
            if (i == criticalStep * 2) {
                criticalStep *= 2;
                critical = i;
            }
        }

        return false;
    }

    inline int evaluate(vector<C> &seq) const {
        unsigned int criticalStep = 8;
        unsigned int critical = criticalStep;

        for (unsigned int i = 0; i < s.high; ++i) {
            // test the stop condition and eventually continue a little bit
            if (norm(seq[i]) > 8.0)
                return i - 1;

            if (cyclic(seq, i, critical, criticalStep))
                return -1;

            // next_point( seq[i], begin );
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }

        return -1;
    }

    inline int evaluate(vector<C> &seq, unsigned int &calculated) const {
        unsigned int criticalStep = 8;
        unsigned int critical = criticalStep;

        for (unsigned int i = 0; i < s.high; ++i) {
            // test the stop condition and eventually continue a little bit
            if (norm(seq[i]) > 8.0) {
                calculated = i;
                return i - 1;
            }

            if (cyclic(seq, i, critical, criticalStep)) {
                calculated = i;
                return -1;
            }

            // next_point( seq[i], begin );
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }

        calculated = s.high;
        return -1;
    }

    // Keep the recurrence in scalar registers and reuse the squares for the
    // escape test and next real coordinate.
    inline int evaluate(vector<C> &seq, unsigned int &contribute, unsigned int &calculated) const {
#if defined(__clang__)
        // Match the separate multiply/add emitted for the original renderer.
        // Fusing them changes rounding and can alter an entire seeded chain.
#pragma clang fp contract(off)
#endif
        unsigned int criticalStep = 8;
        unsigned int critical = criticalStep;
        const double cr = seq[0].real(), ci = seq[0].imag();
        double re = cr, im = ci;
        contribute = 0;

        for (unsigned int i = 0; i < s.high; ++i) {
            if (inside(seq[i]))
                ++contribute;

            const double rr = re * re, ii = im * im;
            if (rr + ii > 8.0) {
                calculated = i;
                return static_cast<int>(i) - 1;
            }

            if (cyclic(seq, i, critical, criticalStep)) {
                calculated = i;
                return -1;
            }

            const double next_im = (re * im + im * re) + ci;
            re = (rr - ii) + cr;
            im = next_im;
            seq[i + 1] = C(re, im);
        }

        calculated = s.high;
        return -1;
    }

    const settings &s;
};

#endif
