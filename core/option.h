#ifndef OPTION_H
#define OPTION_H

#include <string>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

class Option {
	const char* option;
	const char* description;
	boost::any default_value;
	boost::any target_variable;	// a pointer

    static const size_t precision = 15;
	//void* target;	// quite ugly but I don't find any other solution

	void init( const char* l, const char* d );
public:
	Option ( const char* l, const char*  d );
	Option ( const char* l, const char*  d, uint value, uint *t );
	Option ( const char* l, const char* d, double value, double* t );
	Option ( const char* l,  const char* d, int value, int* t );

	void add ( po::options_description_easy_init desc );

	string current_value ( ) const;
	string name ( ) const;
};

#endif // OPTION_H
