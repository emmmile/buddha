#ifndef BUDDHA_TIMER_H
#define BUDDHA_TIMER_H

#include <boost/date_time/posix_time/posix_time.hpp>
namespace pt = boost::posix_time;

struct buddha_timer {
    pt::ptime start;

    buddha_timer ( );
    void restart ( );
    double elapsed ( );

    static unsigned long int microseconds ( );
};



#endif // BUDDHA_TIMER_H
