
#include "core/buddha.h"
#include "core/settings_parser.h"


int main ( int argc, char** argv ) {
	buddha b;
    settings_parser parser( argc, argv );
    b.run( parser() );


    return 0;
}
