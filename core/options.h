#ifndef OPTIONS_H
#define OPTIONS_H

#include "option.h"
#include <vector>
using namespace std;

class Options {
	vector<Option> options;
public:
    Options(  int argc, char **argv );

	void load ( const string& filename );
	void save ( const string& filename );
};


#endif // OPTIONS_H
