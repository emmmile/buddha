
#include "core/buddha.h"
#include "core/settings.h"


int main ( int argc, char** argv ) {
    buddha b;
    settings options( b, argc, argv );
    b.run( );


    return 0;
}
