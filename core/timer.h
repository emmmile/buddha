#ifndef BUDDHA_TIMER_H
#define BUDDHA_TIMER_H

#include <chrono>

struct timer {
    using clock = std::chrono::steady_clock;

    timer() : start(clock::now()) {}

    double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start).count();
    }

    double restart() {
        const auto end = clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();
        start = end;
        return seconds;
    }

private:
    clock::time_point start;
};

#endif
