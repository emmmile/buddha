

#ifndef STATICUTILS_H
#define STATICUTILS_H

#include <iostream>

#define sqr( x )	({ typeof(x) __x = (x); __x * __x; })

#define xstr(s) str(s)
#define str(s) #s


#if TEST > 0
//# undef _print
# define _print( str )		std::cout << str << endl;
#else
//# undef _print
# define _print( str )		;
#endif




double ttime ( void );

#endif
