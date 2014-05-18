#include <array>
#include <set>
#define BOOST_TEST_DYN_LINK        // this is optional
#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_MODULE MyTest
#include <boost/test/unit_test.hpp>
using namespace std;

typedef set<int> testcontainer;


BOOST_AUTO_TEST_CASE( empty )
{
    testcontainer test;

    BOOST_CHECK( test.size() == 0 );
    BOOST_CHECK( test.begin() == test.end() );
}
