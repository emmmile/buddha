#include "option.h"
#include "controlWindow.h"
using boost::any_cast;


void Option::set( const char* l, const char* d, void* t ) {
	this->option = l;
	this->description = d;
	this->target = t;
}

Option::Option ( const char* l, const char*  d ) {
	this->option = l;
	this->description = d;
}

Option::Option ( const char* l, const char*  d, unsigned int value, void* t ) {
	set(l,d,t);
	default_value = value;
}

Option::Option ( const char* l, const char* d, double value, void* t ) {
	set(l,d,t);
	default_value = value;
}

Option::Option ( const char* l,  const char* d, int value, void* t ) {
	set(l,d,t);
	default_value = value;
}

string Option::current_value ( ) {
	stringstream out;
	out.precision( PRECISION );

	if ( default_value.type() == typeid( int ) ) {
		out << *((int*) target);
	} else if ( default_value.type() == typeid( uint ) ) {
		out << *((uint*) target);
	} else if ( default_value.type() == typeid( double ) ) {
		out << *((double*) target);
	}

	return out.str();
}

string Option::name ( ) {
	char* out = strdup( option );
	*( strchrnul( out, ',' ) ) = '\0';
	return out;
}

void Option::add ( po::options_description_easy_init desc ) {

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
