#include "buddha_timer.h"

buddha_timer::buddha_timer() {
    start = std::chrono::high_resolution_clock::now();
}

void buddha_timer::restart() {
    start = std::chrono::high_resolution_clock::now();
}

double buddha_timer::elapsed() {
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000000.0;
}

long unsigned int buddha_timer::microseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>
           (std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}
