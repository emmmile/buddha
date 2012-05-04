#include "options.h"
#include "controlWindow.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/any.hpp>
#include <sstream>
#include <iostream>
using boost::property_tree::ptree;
using namespace std;



Options::Options( ControlWindow* p, int argc, char** argv ) : desc( "Allowed options" ) {
	this->parent = p;
	this->parent->options = this;

	this->operator()
		( "help,h", "produce help message" )
		// doing like this is redundant I know, but there is no way to extract back the target variable
		// prom the po::value class, or the default value
		("red-min,r", po::value<uint>( &p->lowr )->default_value( 0u ), "set low red iterations count", &p->lowr, 0u )
		("green-min,g", po::value<uint>( &p->lowg )->default_value( 0u ), "set low green iterations count", &p->lowg, 0u )
		("blue-min,b", po::value<uint>( &p->lowb )->default_value( 0u ), "set low blue iterations count", &p->lowb, 0u )
		("red-max,R", po::value<uint>(  &p->highr )->default_value( 8192u ), "set high red iterations count", &p->highr, 8192u )
		("green-max,G", po::value<uint>( &p->highg  )->default_value( 2048u ), "set high green iterations count", &p->highg, 2048u )
		("blue-max,B", po::value<uint>(  &p->highb )->default_value( 512u ), "set high blue iterations count", &p->highb, 512u )
		("cre", po::value<double>( &p->cre )->default_value( 0.0 ), "set iniatial real center of the image", &p->cre, 0.0 )
		("cim", po::value<double>(  &p->cim )->default_value( 0.0 ), "set iniatial imaginary center of the image", &p->cim, 0.0 )
		("scale,s", po::value<double>( &p->scale )->default_value( 200.0 ), "set iniatial scale factor of the image", &p->scale, 200.0 )
		("lightness,l", po::value<int>( &p->lightness )->default_value( 60 ), "set the lightness of the image", &p->lightness, 60 )
		("contrast,c", po::value<int>(  &p->contrast )->default_value( 75 ), "set the contrast of the image", &p->contrast, 75 )
		("frame-rate,f", po::value<double>( &p->fps )->default_value( 2.0 ), "set the frame rate of the rendering", &p->fps, 2.0 )
		( "load", po::value<string>(), "load a configuration file" )
	;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	//for ( uint i = 0; i < options.size(); ++i )
	//	cout << options[i].current_value() << endl;

	if ( vm.count("help") ) {
		cout << desc << "\n";
		exit( 0 );
	}

	if ( vm.count("load") ) {
		// load from file
	}
}


Options& Options::operator ()( const char* a, const char* b ) {
	this->desc.add_options()( a, b );
	//this->options.push_back( Option( a, b ) );
	return *this;
}

Options& Options::operator()(const char * a, const po::value_semantic * b , const char * c) {
	this->desc.add_options()( a, b, c );
	return *this;
}

Options& Options::operator()(const char * a, const po::value_semantic * b, const char * c, boost::any d, boost::any e) {
	this->desc.add_options()( a, b, c );
	//cout << int ( d.type() == typeid( uint* ) ) << endl;
	this->options.push_back( Option( a, c, d, e ) );
	return *this;
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
