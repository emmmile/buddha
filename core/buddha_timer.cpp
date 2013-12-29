#include "buddha_timer.h"

buddha_timer::buddha_timer() {
    start = pt::microsec_clock::universal_time();
}

void buddha_timer::restart() {
    start = pt::microsec_clock::universal_time();
}

double buddha_timer::elapsed() {
    pt::ptime end = pt::microsec_clock::universal_time();
    return (end - start).total_microseconds() / 1000000.0;
}
