#include "option.h"
#include "controlWindow.h"
using boost::any_cast;


void Option::init( const char* l, const char* d ) {
	this->option = l;
	this->description = d;
}

Option::Option ( const char* l, const char*  de, boost::any t, boost::any d) {
	this->init( l, de );
	this->target_variable = t;
	this->default_value = d;
}

// look into the target variable and print out the value
string Option::current_value ( ) const {
	stringstream out;
	out.precision( PRECISION );

	// another check could be control the type of target
	if ( default_value.type() == typeid( int ) ) {
		out << *( any_cast<int*>(target_variable) );
	} else if ( default_value.type() == typeid( uint ) ) {
		out << *( any_cast<uint*>(target_variable) );
	} else if ( default_value.type() == typeid( double ) ) {
		out << *( any_cast<double*>(target_variable) );
	} else {
		cerr << "Error in Option::current_value()\n";
		exit( 1 );
	}

	return out.str();
}

string Option::name ( ) const {
	char* out = strdup( option );
	*( strchrnul( out, ',' ) ) = '\0';
	return out;
}
