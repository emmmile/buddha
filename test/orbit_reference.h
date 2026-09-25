#ifndef BUDDHA_ORBIT_REFERENCE_H
#define BUDDHA_ORBIT_REFERENCE_H

#include "settings.h"
#include <cfloat>
#include <vector>

// Frozen pre-optimization evaluator. Keep its operation order, contribution
// boundaries and periodicity test independent of the production implementation.
// Test/benchmark targets disable FP contraction so inlining std::complex cannot
// fuse operations that were separate in the original AppleClang renderer.
template <class C> struct orbit_reference {
    const settings &s;

    int evaluate(std::vector<C> &seq, unsigned int &contribute, unsigned int &calculated) const {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
        unsigned int criticalStep = 8;
        unsigned int critical = criticalStep;
        contribute = 0;
        for (unsigned int i = 0; i < s.high; ++i) {
            const auto &c = seq[i];
            if (c.real() <= s.maxre && c.real() >= s.minre &&
                ((c.imag() <= s.maxim && c.imag() >= s.minim) ||
                 (s.symmetric_image && -c.imag() <= s.maxim && -c.imag() >= s.minim)))
                ++contribute;
            if (norm(seq[i]) > 8.0) {
                calculated = i;
                return static_cast<int>(i) - 1;
            }
            if (i > criticalStep) {
                double distance = norm(seq[i] - seq[critical]);
                if (distance < FLT_EPSILON * FLT_EPSILON) {
                    calculated = i;
                    return -1;
                }
                if (i == criticalStep * 2) {
                    criticalStep *= 2;
                    critical = i;
                }
            }
            seq[i + 1] = seq[i] * seq[i] + seq[0];
        }
        calculated = s.high;
        return -1;
    }
};

#endif
