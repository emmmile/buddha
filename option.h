#ifndef OPTION_H
#define OPTION_H

#include <string>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

class Option {
public:
	const char* option;
	const char* description;
	boost::any default_value;
	void* target;	// quite ugly but I don't find any other solution

	void set( const char* l, const char* d, void* t );

	Option ( const char* l, const char*  d );
	Option ( const char* l, const char*  d, unsigned int value, void* t );
	Option ( const char* l, const char* d, double value, void* t );
	Option ( const char* l,  const char* d, int value, void* t );

	string current_value ( );
	string name ( );
	void add ( po::options_description_easy_init desc );
};

#endif // OPTION_H
