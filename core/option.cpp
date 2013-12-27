#include "option.h"
using boost::any_cast;


void Option::init( const char* l, const char* d ) {
    this->option = l;
    this->description = d;
}

Option::Option ( const char* l, const char*  d ) {
    init( l, d );
}

Option::Option ( const char* l, const char*  d, uint value, uint* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

Option::Option ( const char* l, const char* d, double value, double* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

Option::Option ( const char* l,  const char* d, int value, int* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

string Option::current_value ( ) const {
    stringstream out;
    out.precision( precision );

    // another check could be control the type of target
    if ( default_value.type() == typeid( int ) ) {
        out << *( any_cast<int*>(target_variable) );
    } else if ( default_value.type() == typeid( uint ) ) {
        out << *( any_cast<uint*>(target_variable) );
    } else if ( default_value.type() == typeid( double ) ) {
        out << *( any_cast<double*>(target_variable) );
    }


    return out.str();
}

string Option::name ( ) const {
    char* out = strdup( option );
    //*( strchrnul( out, ',' ) ) = '\0';
    char* end = strchr( out, ',' );
    if ( !end ) end = out + strlen( out );
    *end = '\0';
    return out;
}

void Option::add ( po::options_description_easy_init desc ) {
#define add_option( T ) \
    desc( option, po::value<T>( any_cast<T*>( target_variable ) )-> \
    default_value( any_cast<T>( default_value ) ), description )

    if ( default_value.type() == typeid( int ) ) {
        add_option( int );
    } else if ( default_value.type() == typeid( uint ) ) {
        add_option( uint );
    } else if ( default_value.type() == typeid( double ) ) {
        add_option( double );
    } else if ( default_value.empty() ) {
        desc( option, description );
    } else {
        BOOST_LOG_TRIVIAL(fatal) << "Option::add(), error in option creation";
        exit( 1 );
    }
}
