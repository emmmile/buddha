
#include <functional>
#include "core/buddha.h"
#include "core/settings.h"
#include <signal.h>

int main ( int argc, char** argv ) {
    signal(SIGINT, [](int signum) { cout << "SIGINT" << endl; });

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);


    buddha b;
    settings options( b, argc, argv );
    b.run( );


    return 0;
}
