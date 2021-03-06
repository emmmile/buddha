#include "options.h"
#include "controlWindow.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/any.hpp>
#include <sstream>
#include <iostream>
namespace po = boost::program_options;
using boost::property_tree::ptree;
using namespace std;



Options::Options( ControlWindow* parent, int argc, char** argv ) {
	this->parent = parent;
	this->parent->options = this;
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
