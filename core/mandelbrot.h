#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <bitset>
#include <vector>
#include <thread>
#include <iostream>


#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
//#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>

#include <boost/filesystem.hpp>

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/png_dynamic_io.hpp>
using namespace boost::gil;
namespace bar = boost::archive;
namespace bio = boost::iostreams;

#include "settings.h"
#include "timer.h"
using namespace std;

template<class C, unsigned int N = 4096> // complex type
class mandelbrot {
public:
	struct exclusion {
		const string filename;
		vector<bool> data;

		exclusion( const mandelbrot& core) : filename("exclusion.map") {
			data.resize(N * N / 2);


			if ( !load() ) {

				// compute the exclusion map
	    		BOOST_LOG_TRIVIAL(debug) << "generating " << N << "x" << N << " exclusion map";

	    		timer time;
	    		// rgb8_image_t img(N, N);
			    // rgb8_image_t::view_t v = view(img);

			    unsigned blocks = 16;
			    unsigned int block = N * N / 2 / blocks;
			    vector<thread> threads;
			    for ( unsigned int t = 0; t < blocks; ++t )
			    	threads.emplace_back(&exclusion::compute, this, core, t * block, t * block + block);

			    for ( unsigned int t = 0; t < blocks; ++t )
			    	threads[t].join();

				BOOST_LOG_TRIVIAL(debug) << "generated exclusion map in " << time.elapsed() << " s";
				time.restart();

				/* to be even more conservative remove 1 pixel from the borders
				vector<bool> newone;
				newone.resize(N * N / 2);
			    for ( unsigned int i = 0; i < N * N / 2; ++i ) {
			    	int x = i % N;
			    	int y = i / N;
					int radius = 1;

					if ( x < radius || y < radius || x >= N - radius || y >= N / 2 - radius || !data[i] ) {
						newone[i] = data[i];
					} else {
						int total = 0;
						for ( int dx = -radius; dx <= radius; ++dx )
							for ( int dy = -radius; dy <= radius; ++dy )
								total += !data[index(x + dx, y + dy)];
						
						newone[i] = (total == 0);
					}

					// if ( newone[i] ) v(x, y) = v(x, N-y-1) = rgb8_pixel_t(255,255,255);
					// else v(x,y) = rgb8_pixel_t(0,0,0);
				}
				data = newone;*/

				// png_write_view("exclusion.png", const_view(img));
				BOOST_LOG_TRIVIAL(debug) << "stripped additional pixels in " << time.elapsed() << " s";
			}
		}

		void compute ( const mandelbrot& core, unsigned int beg, unsigned end ) {
			vector<C> seq;
			seq.resize(core.high + 1);

			for ( unsigned int i = beg; i < end; ++i ) {
		    	seq[0] = point(i);
				data[i] = (core.evaluate(seq) == -1);
		    }
		}

		inline unsigned int index ( unsigned int x, unsigned int y ) const {
			return y * N + x;
		}

		inline unsigned int index ( const C& c ) const {
			int xx = c.real() * N / 4.0 + N / 2.0;
			int yy = -fabs(c.imag()) * N / 4.0 + N / 2.0;
			
			return index(xx, yy);
		}

		C point ( unsigned int i ) const {
			int x = i % N - int(N) / 2;
			int y = i / N - int(N) / 2;

			return C(x * 4.0 / N, y * 4.0 / N);
		}

		bool load ( ) {
			if ( !boost::filesystem::exists( filename ) )
				return false;

			std::ifstream iss( filename, ios::in | ios::binary);
			bio::filtering_stream<bio::input> f;
			f.push(bio::gzip_decompressor());
			f.push(iss);
			bar::binary_iarchive ia(f);
			
	    	BOOST_LOG_TRIVIAL(debug) << "loading " << N << "x" << N << " exclusion map";
	    	timer time;
	    	unsigned int size;
	    	ia >> size;
	    	assert(size == N * N / 2);
			ia >> data;
	    	BOOST_LOG_TRIVIAL(debug) << "loaded exclusion map in " << time.elapsed() << " s";
			return true;
		}

		void save ( ) {
			if ( boost::filesystem::exists( filename ) )
				return;

			std::ofstream oss( filename, std::ios::binary);
			bio::filtering_stream<bio::output> f;
			f.push(bio::gzip_compressor());
			f.push(oss);
			bar::binary_oarchive oa(f);

	    	BOOST_LOG_TRIVIAL(debug) << "saving " << N << "x" << N << " exclusion map";
	    	timer time;
	    	unsigned int size = N * N / 2;
	    	oa << size;
			oa << data;
	    	BOOST_LOG_TRIVIAL(debug) << "saved exclusion map in " << time.elapsed() << " s";
		}

		inline bool operator()( const C& c) const {
			int xx = c.real() * N / 4.0 + N / 2.0;
			int yy = -fabs(c.imag()) * N / 4.0 + N / 2.0;
			if ( xx < 0 || xx >= N || yy < 0 || yy >= N/2 ) return false;

			return data[index(xx, yy)];
		}
	};







	mandelbrot( unsigned int low, unsigned int high ) 
		: low(low), high(high), map(*this) {
	}

	inline bool inside ( C& c, const settings& s ) const {
		return  c.real() <= s.maxre && c.real() >= s.minre &&
            ( ( c.imag() <= s.maxim && c.imag() >= s.minim ) ||
              ( -c.imag() <= s.maxim && -c.imag() >= s.minim ) );
	}

	inline bool cyclic( const vector<C>& seq, const unsigned int& i, unsigned int& critical, unsigned int& criticalStep ) const {
		if ( i > criticalStep ) {
            // compute the distance from the critical point
            double distance = norm( seq[i] - seq[critical] );

            // if I found that two calculated points are very very close I conclude that
            // they are the same point, so the sequence is periodic so we are computing a point
            // in the mandelbrot, so I stop the calculation
            if ( distance < FLT_EPSILON * FLT_EPSILON ) { // maybe also DBL_EPSILON is sufficient
                return true;
            }

            // I don't do this step at every iteration to be more fast, I found that a very good
            // compromise is to use a multiplicative distance between each checkpoint
            if ( i == criticalStep * 2 ) {
                criticalStep *= 2;
                critical = i;
            }
        }

        return false;
	}





	inline int evaluate ( vector<C>& seq ) const {
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;

	   	//if ( map(seq[0]) )
	    //	return -1;


	    for ( unsigned int i = 0; i < high; ++i ) {
	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 )
	            return i - 1;

	        if ( cyclic( seq, i, critical, criticalStep) ) 
	            return -1;
	        
	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    return -1;
	}


	inline int evaluate ( vector<C>& seq, unsigned int& calculated ) const {
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;

	   	if ( map(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }


	    for ( unsigned int i = 0; i < high; ++i ) {
	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 ) {
	            calculated = i;
	            return i - 1;
	        }

	        if ( cyclic( seq, i, critical, criticalStep) ) {
	            calculated = i;
	            return -1;
	        }
	        
	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    calculated = high;
	    return -1;
	}


	// this is the main function. Here little modifications impacts a lot on the speed of the program!
	int evaluate ( vector<C>& seq, unsigned int& contribute, unsigned int& calculated, const settings& s ) const {
	    unsigned int criticalStep = 8;
	    unsigned int critical = criticalStep;
	    contribute = 0;

	    if ( map(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

	    for ( unsigned int i = 0; i < high; ++i ) {
	        //cout << i << " " << seq[i] << endl;
	        //getchar();

	        // this checks if the seq[i] point is inside the screen
	        if ( inside( seq[i], s ) )
	            ++contribute;

	        // test the stop condition and eventually continue a little bit
	        if ( norm( seq[i] ) > 8.0 ) {
	            calculated = i;
	            return i - 1;
	        }
	        
	        if ( cyclic( seq, i, critical, criticalStep) ) {
	            calculated = i;
	            return -1;
	        }

	        //next_point( seq[i], begin );
	        seq[i+1] = seq[i] * seq[i] + seq[0];
	    }

	    calculated = high;
	    return -1;
	}

	unsigned int low;
	unsigned int high;
	exclusion map;
};

#endif