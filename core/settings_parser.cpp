
#include <iostream>
#include <sstream>
#include <thread>

#include "settings_parser.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
namespace po = boost::program_options;
using boost::property_tree::ptree;
using namespace std;



settings_parser::settings_parser( int argc, char** argv ) {
    po::options_description desc("Allowed options");
    options.push_back( Option("red-min,r", "set low iterations count", 512u, &s.low ) );
    options.push_back( Option("red-max,R", "set high iterations count", 8192u, &s.high ) );
    options.push_back( Option("cre", "set iniatial real center of the image", 0.0, &s.cre ) );
    options.push_back( Option("cim", "set iniatial imaginary center of the image", 0.0, &s.cim ) );
    options.push_back( Option("scale,s", "set iniatial scale factor of the image", 800.0, &s.scale ) );
    options.push_back( Option("lightness,l", "set the lightness of the image", 100, &s.lightness) );
    options.push_back( Option("contrast,c", "set the contrast of the image", 100, &s.contrast) );
    options.push_back( Option("threads,t", "set the number of parallel threads", std::thread::hardware_concurrency(), &s.threads) );
    options.push_back( Option("width,w", "width of the output", 3000, &s.w) );
    options.push_back( Option("height,h", "height of the output", 2000, &s.h) );
    options.push_back( Option("out,o", "output filename", "output", &s.outfile) );
    options.push_back( Option("load,L", "try to load a previously saved state", "", &s.infile) );
    options.push_back( Option("help", "produce help message" ) );
    options.push_back( Option("inverse", "produce the anti-buddhabrot" ) );
    options.push_back( Option("formula", "specify the formula to evaluate", "z = z * z + c", &s.formula) );

    for ( uint i = 0; i < options.size(); ++i )
        options[i].add( desc.add_options() );


    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);


    if ( vm.count("help") ) {
        cout << desc << "\n";
        exit( 0 );
    }

    s.inverse = vm.count("inverse") ? true : false;
    s.indirect_settings();
}

settings_parser::settings_parser( const string& filename ) {
    load( filename );
}


settings settings_parser::operator() ( ) {
    return s;
}

void settings_parser::save(const string &filename) {
    ptree pt;

    for ( uint i = 0; i < options.size(); ++i ) {
        string val = options[i].current_value();
        if ( val == "" ) continue;
        pt.put("buddha." + options[i].name(), val );
    }

    // Write the property tree to the XML file.
    write_xml(filename, pt);
}

void settings_parser::load(const string &filename) {
    // Create an empty property tree object
    ptree pt;

    // Load the XML file into the property tree. If reading fails
    // (cannot open file, parse error), an exception is thrown.
    read_xml(filename, pt);

    // put the tree on the options
}
