#include "options.h"
#include "controlWindow.h"
//#include <boost/property_tree/ptree.hpp>
//#include <boost/property_tree/xml_parser.hpp>
//#include <boost/any.hpp>
#include <sstream>
#include <iostream>
//namespace po = boost::program_options;
//using boost::property_tree::ptree;
using namespace std;



Options::Options( ControlWindow* parent, int argc, char** argv ) {
	this->parent = parent;
    this->parent->options = this;
    parent->lowr = 0;
    parent->lowg = 0;
    parent->lowb = 0;
    parent->highr = 512;
    parent->highg = 2048;
    parent->highb = 8192;
    parent->cre = 0.0;
    parent->cim = 0.0;
    parent->scale = 200;
    parent->lightness = 60;
    parent->contrast = 75;
    parent->fps = 2.0;
}



void Options::save(const string &filename) {
    /*ptree pt;

	for ( uint i = 0; i < options.size(); ++i ) {
		string val = options[i].current_value();
		if ( val == "" ) continue;
		pt.put("buddha." + options[i].name(), val );
	}

	// Write the property tree to the XML file.
    write_xml(filename, pt);*/
}

void Options::load(const string &filename) {
	// Create an empty property tree object
    /*ptree pt;

	// Load the XML file into the property tree. If reading fails
	// (cannot open file, parse error), an exception is thrown.
    read_xml(filename, pt);*/
}
