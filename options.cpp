#include "options.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/any.hpp>
#include <sstream>
#include <iostream>
namespace po = boost::program_options;
using boost::property_tree::ptree;
using boost::any_cast;
using namespace std;





class Option {
public:
	const char* option;
	const char* description;
	boost::any default_value;
	void* target;	// quite ugly but I don't find any other solution

	void set( const char* l, const char* d, void* t ) {
		this->option = l;
		this->description = d;
		this->target = t;
	}

	Option ( const char* l, const char*  d ) {
		this->option = l;
		this->description = d;
	}

	Option ( const char* l, const char*  d, unsigned int value, void* t ) {
		set(l,d,t);
		default_value = value;
	}

	Option ( const char* l, const char* d, double value, void* t ) {
		set(l,d,t);
		default_value = value;
	}

	Option ( const char* l,  const char* d, int value, void* t ) {
		set(l,d,t);
		default_value = value;
	}

	string current_value ( ) {
		stringstream out;

		if ( default_value.type() == typeid( int ) ) {
			out << any_cast<int>( default_value );
		} else if ( default_value.type() == typeid( uint ) ) {
			out << any_cast<uint>( default_value );
		} else if ( default_value.type() == typeid( double ) ) {
			out << any_cast<double>( default_value );
		}

		return out.str();
	}

	string name ( ) {
		char* out = strdup( option );
		*( strchrnul( out, ',' ) ) = '\0';
		return out;
	}

	void add ( po::options_description_easy_init desc ) {

		if ( default_value.type() == typeid( int ) ) {
			po::typed_value<int>* out =
			po::value<int>((int*)target)->default_value( any_cast<int>( default_value ) );
			desc( option, out, description );
		} else if ( default_value.type() == typeid( uint ) ) {
			po::typed_value<uint>* out =
			po::value<uint>((uint*)target)->default_value( any_cast<uint>( default_value ) );
			desc( option, out, description );
		} else if ( default_value.type() == typeid( double ) ) {
			po::typed_value<double>* out =
			po::value<double>((double*)target)->default_value( any_cast<double>( default_value ) );
			desc( option, out, description );
		} else if ( default_value.empty() ) {
			desc( option, description );
		} else {
			cerr << "Error in option creation.\n";
			exit( 1 );
		}
	}
};






Options::Options( ControlWindow* parent, int argc, char** argv ) {
	this->parent = parent;
	po::options_description desc("Allowed options");
	options.push_back( Option("red-min,r", "set low red iterations count", 0u, &parent->lowr ) );
	options.push_back( Option("green-min,g", "set low green iterations count", 0u, &parent->lowg ) );
	options.push_back( Option("blue-min,b", "set low blue iterations count", 0u, &parent->lowb ) );
	options.push_back( Option("red-max,R", "set high red iterations count", 512u, &parent->highr ) );
	options.push_back( Option("green-max,G", "set high green iterations count", 2048u, &parent->highg ) );
	options.push_back( Option("blue-max,B", "set high blue iterations count", 8192u, &parent->highb ) );
	options.push_back( Option("cre", "set iniatial real center of the image", 0.0, &parent->cre ) );
	options.push_back( Option("cim", "set iniatial imaginary center of the image", 0.0, &parent->cim ) );
	options.push_back( Option("scale,s", "set iniatial scale factor of the image", 200.0, &parent->scale ) );
	options.push_back( Option("lightness,l", "set the lightness of the image", 60, &parent->lightness) );
	options.push_back( Option("contrast,c", "set the contrast of the image", 75, &parent->contrast) );
	options.push_back( Option("frame-rate,f", "set the frame rate of the rendering", 2.0, &parent->fps) );
	options.push_back( Option("help,h", "produce help message" ) );

	for ( uint i = 0; i < options.size(); ++i )
		options[i].add( desc.add_options() );


	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);


	if ( vm.count("help") ) {
		cout << desc << "\n";
		exit( 0 );
	}
}



void Options::save(const string &filename) {
	ptree pt;

	for ( uint i = 0; i < options.size(); ++i ) {
		string val = options[i].current_value();
		if ( val == "" ) continue;
		pt.put("buddha." + options[i].name(), val );
	}

	// Write the property tree to the XML file.
	write_xml(filename, pt);
}

void Options::load(const string &filename) {
	// Create an empty property tree object
	ptree pt;

	// Load the XML file into the property tree. If reading fails
	// (cannot open file, parse error), an exception is thrown.
	read_xml(filename, pt);
}
