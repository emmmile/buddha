
#include "core/buddha.h"
#include "core/settings_parser.h"


int main ( int argc, char** argv ) {
    settings_parser parser( argc, argv );
    buddha b( parser() );
    b.run();


    return 0;
}
