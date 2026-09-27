/*
 * Copyright (c) 2010, Emilio Del Tessandoro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY EMILIO DEL TESSANDORO o ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL EMILIO DEL TESSANDORO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <random>
#include "buddha_generator.h"
#define METTHD 16000

using namespace std;

buddha_generator::~buddha_generator() {
    // BOOST_LOG_TRIVIAL(debug) << "buddha_generator::~buddha_generator";
}

buddha_generator::buddha_generator(mandelbrot<complex_type> &core, vector_type &raw,
                                   const settings &s, uint64_t seed)
    : core(core), raw(raw), s(s), computed(0), find_attempts(0), proposals(0), accepted(0),
      drawn_orbits(0), generator(), uniform(0, 1), normal(0, 1), exponential(2) {
    generator.seed(seed);

    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::initialize() with seed " << seed;

    seq.resize(s.high + 1);

    finish = false;
}

void buddha_generator::start() { t = thread(&buddha_generator::run, this); }

void buddha_generator::stop() {
    lock_guard<mutex> locker(execution);
    finish = true;
}

namespace {
// Histogram access for buddha_kernel::draw.
struct raw_histogram {
    buddha::vector_type &raw;
    uint64_t width;
    void add(uint32_t x, uint32_t y, uint32_t channel, uint32_t weight) {
        raw[(uint64_t(y) * width + x) * 3 + channel].add(weight);
    }
};
} // namespace

// Mapping, mirroring and the odd-height centre-row weight are shared with buddha-metal.
void buddha_generator::drawPoint(complex_type &c, bool drawr, bool drawg, bool drawb) {
    const buddha_kernel::geometry<double> g = s.histogram_geometry();
    raw_histogram histogram{raw, s.w};
    buddha_kernel::draw(g, c.real(), c.imag(), drawr, drawg, drawb, histogram);
}

inline void buddha_generator::gaussianMutation(complex_type &z, double radius) {
    double amplitude = fabs(normal(generator)) * radius;
    double phase = uniform(generator) * 2 * M_PI;

    z += std::polar(amplitude, phase);
}

inline void buddha_generator::exponentialMutation(complex_type &z, double radius) {
    double amplitude = fabs(exponential(generator)) * radius;
    double phase = uniform(generator) * 2 * M_PI;

    z += std::polar(amplitude, phase);
}

// search for a point that falls in the screen, simply moves randomly making moves
// proportional in size to the distance from the center of the screen.
// I think can be optimized a lot
int buddha_generator::findPoint(complex_type &begin, unsigned int &contribute,
                                unsigned int &calculated) {
    int max, iterations = 0;
    unsigned int calculatedInThisIteration;
    complex_type tmp = begin;

    // 64 - 512
#define FINDPOINTMAX 256

    calculated = 0;
    do {
        ++find_attempts;
        gaussianMutation(tmp, 1.0);
        seq[0] = tmp;

        max = core.evaluate(seq, contribute, calculatedInThisIteration);
        calculated += calculatedInThisIteration;

        begin = tmp;
    } while (!contribute && ++iterations < FINDPOINTMAX);

    // BOOST_LOG_TRIVIAL(info) << iterations;
    return max;
}

// the metropolis algorithm. I don't know very much about the teory under this optimization but I
// think is implemented quite well.. Maybe a better method for the transition probability can be
// found but I don't know.
void buddha_generator::metropolis() {
    complex_type begin(0.0, 0.0);
    unsigned int calculated, selectedOrbitCount = 0, proposedOrbitCount = 0;
    int selectedOrbitMax = 0, proposedOrbitMax = 0, j;
    double radius = 20.0 / s.scale; // 100.0;

    // search a point that has some contribute in the interested area
    selectedOrbitMax = findPoint(begin, selectedOrbitCount, calculated);
    computed += calculated;
    // cout << selectedOrbitMax << endl;

    // if the search failed I exit
    if (selectedOrbitCount == 0)
        return;

    complex_type ok = begin;
    // also "how much" cicles are executed on each point is crucial. In order to have more points on
    // the screen an high iteration count could be better but, not too high because otherwise the
    // space is not sampled well. I tried values between 512 and 8192 and they works well. Over
    // 80000 it becames strange. Now i'm using something proportional on "how much the point is
    // important".. For example how long the sequence is and how many points falls on the window.
    for (j = 0; j < max((int)selectedOrbitCount * 256, selectedOrbitMax * 2); j++) {
        begin = ok;
        // the radius of the mutations influences a lot the quality of the rendering AND the speed.
        // I think that choose a random radius is the best way otherwise I noticed some geometric
        // artifacts around the point (-1.8, 0) for example. This artifacts however depend also on
        // the number of iterations explained above.

        // seq[0].mutate( random( &buf ) * radius /* + add */, &buf );
        exponentialMutation(begin, uniform(generator) * radius);
        seq[0] = begin;

        // calculate the new sequence
        ++proposals;
        proposedOrbitMax = core.evaluate(seq, proposedOrbitCount, calculated);

        // the sequence is periodic, I try another mutation
        if (proposedOrbitMax <= 0)
            continue;

        // maybe the sequence is not periodic but It doesn't contribute on the actual region
        if (proposedOrbitCount == 0)
            continue;

        // calculus of the transitional probability. One point is more probable of being
        // chose if generates a lot of points in the window
        double alpha = (double(proposedOrbitMax) * proposedOrbitMax * proposedOrbitCount) /
                       (double(selectedOrbitMax) * selectedOrbitMax * selectedOrbitCount);

        if (alpha > uniform(generator)) {
            ok = begin;
            selectedOrbitCount = proposedOrbitCount;
            selectedOrbitMax = proposedOrbitMax;
            ++accepted;
        }

        computed += calculated;

        // draw the points
        lock_guard<mutex> locker(execution);
        ++drawn_orbits;

        for (unsigned int i = s.low;
             int(i) <= proposedOrbitMax && proposedOrbitCount > 0 && i < s.high; i++) {
            drawPoint(seq[i], buddha_kernel::in_channel(i, s.lowr, s.highr),
                      buddha_kernel::in_channel(i, s.lowg, s.highg),
                      buddha_kernel::in_channel(i, s.lowb, s.highb));
        }

        if (finish)
            break;
    }
}

void buddha_generator::naive() {
    const uint64_t key = generator();
    naive(uint32_t(key), uint32_t(key >> 32), naive_batch);
}

void buddha_generator::naive(uint32_t key0, uint32_t key1, uint32_t count) {
    buddha_kernel::parameters p = s.kernel_parameters(uint32_t(core.size));
    p.key0 = key0;
    p.key1 = key1;
    p.count = count;
    const buddha_kernel::geometry<float> g = buddha_kernel::make_geometry<float>(p);
    raw_histogram histogram{raw, s.w};

    buddha_kernel::lane<float> lane;
    lane.begin(0, 1, count);
    while (lane.advance(p, g, core.data, histogram)) {
    }
    computed += lane.t.iterations;
    drawn_orbits += lane.t.escaped;
}

void buddha_generator::run() {
    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run()";
    const bool naive_sampler = s.sampler == "naive";

    while (true) {
        if (naive_sampler)
            naive();
        else
            metropolis();

        lock_guard<mutex> locker(execution);
        if (finish)
            break;
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha_generator::run(), finished";
}
