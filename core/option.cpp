
#include "option.h"
using boost::any_cast;


void Option::init( const char* l, const char* d ) {
    this->option = l;
    this->description = d;
}

Option::Option ( const char* l, const char*  d ) {
    init( l, d );
}

Option::Option ( const char* l, const char*  d, uint32_t value, uint32_t* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

Option::Option ( const char* l, const char*  d, uint64_t value, uint64_t* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

Option::Option ( const char* l, const char* d, double value, double* t ) {
    init( l, d );
    default_value = value;
    target_variable = t;
}

Option::Option (const char* l,  const char* d, const string &value, string *t ) {
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
    } else if ( default_value.type() == typeid( uint32_t ) ) {
        out << *( any_cast<uint32_t*>(target_variable) );
    } else if ( default_value.type() == typeid( uint64_t ) ) {
        out << *( any_cast<uint64_t*>(target_variable) );
    } else if ( default_value.type() == typeid( double ) ) {
        out << *( any_cast<double*>(target_variable) );
    } else if ( default_value.type() == typeid( string ) ) {
        out << *( any_cast<string*>(target_variable) );
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
    } else if ( default_value.type() == typeid( uint32_t ) ) {
        add_option( uint32_t );
    } else if ( default_value.type() == typeid( uint64_t ) ) {
        add_option( uint64_t );
    } else if ( default_value.type() == typeid( double ) ) {
        add_option( double );
    } else if ( default_value.type() == typeid( string ) ) {
        add_option( string );
    } else if ( default_value.empty() ) {
        desc( option, description );
    } else {
        BOOST_LOG_TRIVIAL(fatal) << "Option::add(), error in option creation";
        exit( 1 );
    }
}
