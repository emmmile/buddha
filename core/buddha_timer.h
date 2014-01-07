#ifndef BUDDHA_TIMER_H
#define BUDDHA_TIMER_H

#include <chrono>
using namespace std;

struct buddha_timer {
    chrono::time_point<std::chrono::high_resolution_clock> start;

    buddha_timer ( );
    void restart ( );
    double elapsed ( );

    static unsigned long int microseconds ( );
};

#endif // BUDDHA_TIMER_H
