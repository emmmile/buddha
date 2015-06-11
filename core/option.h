#ifndef OPTION_H
#define OPTION_H

#include <string>
#define BOOST_LOG_DYN_LINK
#include <boost/any.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;


typedef unsigned char uchar;
typedef unsigned int uint;


class Option {
	const char* option;
	const char* description;
	boost::any default_value;
	boost::any target_variable;	// a pointer

    static const uint precision = 15;
	//void* target;	// quite ugly but I don't find any other solution

	void init( const char* l, const char* d );
public:
	Option ( const char* l, const char*  d );
	Option ( const char* l, const char*  d, uint32_t value, uint32_t *t );
	Option ( const char* l, const char*  d, uint64_t value, uint64_t *t );
	Option ( const char* l, const char* d, double value, double* t );
	Option ( const char* l,  const char* d, int value, int* t );
    Option ( const char* l,  const char* d, const string& value, string* t );

    void add ( po::options_description_easy_init desc );

	string current_value ( ) const;
	string name ( ) const;
};

#endif // OPTION_H
