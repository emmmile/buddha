#ifndef MANDELBROT_H
#define MANDELBROT_H


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

#include "mandelbrot_base.h"
using namespace boost::gil;
namespace bar = boost::archive;
namespace bio = boost::iostreams;
using namespace std;



template<class C, unsigned int N = 1024>
struct mandelbrot : public mandelbrot_base<C> {
	const string filename;
	vector<bool> data;

	typedef mandelbrot_base<C> base;

	mandelbrot( unsigned int low, unsigned int high ) : base(low, high), filename("exclusion.map") {
		data.resize(N * N / 2);


		if ( !load() ) {
			// compute the exclusion map
    		BOOST_LOG_TRIVIAL(debug) << "generating " << N << "x" << N << " exclusion map";

    		timer time;

		    unsigned blocks = 16;
		    unsigned int block = N * N / 2 / blocks;
		    vector<thread> threads;
		    for ( unsigned int t = 0; t < blocks; ++t )
		    	threads.emplace_back(&mandelbrot::compute, this, t * block, t * block + block);

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

			BOOST_LOG_TRIVIAL(debug) << "stripped additional pixels in " << time.elapsed() << " s";
		}
	}

	void compute ( unsigned int beg, unsigned end ) {
		vector<C> seq;
		seq.resize(this->high + 1);

		for ( unsigned int i = beg; i < end; ++i ) {
	    	seq[0] = point(i);
			data[i] = (base::evaluate(seq) == -1);
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


		rgb8_image_t img(N, N);
		rgb8_image_t::view_t v = view(img);

		for ( unsigned int i = 0; i < N * N / 2; ++i ) {
			int x = i % N;
			int y = i / N;
			if ( data[i] ) v(x, y) = v(x, N-y-1) = rgb8_pixel_t(255,255,255);
			else v(x,y) = rgb8_pixel_t(0,0,0);
		}

		png_write_view("exclusion.png", const_view(img));


    	BOOST_LOG_TRIVIAL(debug) << "saved exclusion map in " << time.elapsed() << " s";
	}

	inline bool operator()( const C& c) const {
		int xx = c.real() * N / 4.0 + N / 2.0;
		int yy = -fabs(c.imag()) * N / 4.0 + N / 2.0;
		if ( xx < 0 || xx >= int(N) || yy < 0 || yy >= int(N/2) ) return false;

		return data[index(xx, yy)];
	}













	int evaluate ( vector<C>& seq ) const {
	    //if ( (*this)(seq[0]) )
	    //	return -1;

		return base::evaluate( seq );
	}

	int evaluate ( vector<C>& seq, unsigned int& calculated ) const {
	    if ( (*this)(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

		return base::evaluate( seq, calculated );
	}

	int evaluate ( vector<C>& seq, unsigned int& contribute, unsigned int& calculated, const settings& s ) const {
	    if ( (*this)(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

		return base::evaluate( seq, contribute, calculated, s );
	}
};



#endif