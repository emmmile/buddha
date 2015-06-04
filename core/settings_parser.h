#ifndef SETTINGS_PARSER_H
#define SETTINGS_PARSER_H

#include <vector>
#include "settings.h"
#include "option.h"
using namespace std;

class settings_parser {
	settings s;
	vector<Option> options;
public:
    settings_parser( int argc, char **argv );
    settings_parser( const string& filename );

    settings operator() ( );
	void load ( const string& filename );
	void save ( const string& filename );
};


#endif // SETTINGS_PARSER_H
