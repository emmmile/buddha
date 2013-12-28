
#include <functional>
#include "core/buddha.h"
#include "core/settings.h"
#include <signal.h>

int main ( int argc, char** argv ) {

    buddha b;
    settings options( b, argc, argv );
    b.run( );


    return 0;
}
