#ifndef OPTIONS_H
#define OPTIONS_H

//#include "controlWindow.h"
#include "option.h"
#include <vector>
using namespace std;

class ControlWindow;

class Options {
	ControlWindow* parent;
	vector<Option> options;
public:
	Options( ControlWindow* control, int argc, char **argv );

	void load ( const string& filename );
	void save ( const string& filename );
};


#endif // OPTIONS_H
