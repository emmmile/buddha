
#include <iostream>
#include <sstream>
#include <thread>

#include "settings.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
namespace po = boost::program_options;
using boost::property_tree::ptree;
using namespace std;



settings::settings(buddha &parent, int argc, char** argv ) {
    po::options_description desc("Allowed options");
    options.push_back( Option("red-min,r", "set low red iterations count", 0u, &parent.lowr ) );
    options.push_back( Option("green-min,g", "set low green iterations count", 0u, &parent.lowg ) );
    options.push_back( Option("blue-min,b", "set low blue iterations count", 0u, &parent.lowb ) );
    options.push_back( Option("red-max,R", "set high red iterations count", 8192u, &parent.highr ) );
    options.push_back( Option("green-max,G", "set high green iterations count", 2048u, &parent.highg ) );
    options.push_back( Option("blue-max,B", "set high blue iterations count", 512u, &parent.highb ) );
    options.push_back( Option("cre", "set iniatial real center of the image", 0.0, &parent.cre ) );
    options.push_back( Option("cim", "set iniatial imaginary center of the image", 0.0, &parent.cim ) );
    options.push_back( Option("scale,s", "set iniatial scale factor of the image", 800.0, &parent.scale ) );
    options.push_back( Option("lightness,l", "set the lightness of the image", 100, &parent.lightness) );
    options.push_back( Option("contrast,c", "set the contrast of the image", 75, &parent.contrast) );
    options.push_back( Option("threads,t", "set the number of parallel threads", std::thread::hardware_concurrency(), &parent.threads) );
    options.push_back( Option("width,w", "width of the output", 3000, &parent.w) );
    options.push_back( Option("height,h", "height of the output", 2000, &parent.h) );
    options.push_back( Option("out,o", "output filename", "output", &parent.outfile) );
    options.push_back( Option("load,L", "try to load a previously saved state", "", &parent.infile) );
    options.push_back( Option("help", "produce help message" ) );

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

settings::settings( const string& filename ) {
    load( filename );
}

void settings::save(const string &filename) {
    ptree pt;

    for ( uint i = 0; i < options.size(); ++i ) {
        string val = options[i].current_value();
        if ( val == "" ) continue;
        pt.put("buddha." + options[i].name(), val );
    }

    // Write the property tree to the XML file.
    write_xml(filename, pt);
}

void settings::load(const string &filename) {
    // Create an empty property tree object
    ptree pt;

    // Load the XML file into the property tree. If reading fails
    // (cannot open file, parse error), an exception is thrown.
    read_xml(filename, pt);

    // put the tree on the options
}
