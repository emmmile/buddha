#ifndef OPTIONS_H
#define OPTIONS_H

#include <vector>
#include "buddha.h"
#include "option.h"
using namespace std;

class settings {
	vector<Option> options;
public:
    settings(buddha& parent,  int argc, char **argv );
    settings( const string& filename );

	void load ( const string& filename );
	void save ( const string& filename );
};


#endif // OPTIONS_H
