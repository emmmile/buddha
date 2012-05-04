#ifndef OPTIONS_H
#define OPTIONS_H

//#include "controlWindow.h"
#include "option.h"
#include <vector>
using namespace std;
namespace po = boost::program_options;

class ControlWindow;

class Options {
	ControlWindow* parent;
	vector<Option> options;
	po::options_description desc;

	Options& operator()(const char *, const char *);
	Options& operator()(const char *, const po::value_semantic *, const char *);
	Options& operator()(const char *, const po::value_semantic *, const char *, boost::any, boost::any );
public:
	Options( ControlWindow* control, int argc, char **argv );

	void load ( const string& filename );
	void save ( const string& filename );
};


#endif // OPTIONS_H
