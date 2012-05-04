#ifndef OPTION_H
#define OPTION_H

#include <string>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

class Option {
	const char* option;
	const char* description;
	boost::any target_variable;	// a pointer
	boost::any default_value;

	void init( const char* l, const char* d );
public:
	Option ( const char* l, const char* de, boost::any t, boost::any d );

	string current_value ( ) const;
	string name ( ) const;
};

#endif // OPTION_H
