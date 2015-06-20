#ifndef MANDELBROT_H
#define MANDELBROT_H


#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/filesystem.hpp>
#include "timer.h"

#define png_infopp_NULL (png_infopp)NULL
#define int_p_NULL (int*)NULL
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/png_dynamic_io.hpp>

#include "mandelbrot_base.h"
using namespace boost::gil;
namespace bar = boost::archive;
namespace bio = boost::iostreams;
using namespace std;



template<class C>
struct mandelbrot : public mandelbrot_base<C> {
	const uint64_t size;
	vector<bool> data;

	typedef mandelbrot_base<C> base;
    std::default_random_engine generator;
    std::uniform_real_distribution<double> uniform;


	mandelbrot( const settings& s ) : base(s), size(s.exclusion_size), uniform(0, 4.0 / size) {
		data.resize(size * size / 2);
		fill(data.begin(), data.end(), false);
	}

	void exclusion ( ) {
		// compute the exclusion map
		BOOST_LOG_TRIVIAL(debug) << "generating " << size << "x" << size << " exclusion map";

		timer time;

		unsigned blocks = 2 * this->s.threads; // some blocks are easy
		unsigned int block = size * size / 2 / blocks;
		vector<thread> threads;
		for ( unsigned int t = 0; t < blocks; ++t )
			threads.emplace_back(&mandelbrot::compute, this, t * block, t * block + block);

		for ( unsigned int t = 0; t < blocks; ++t )
			threads[t].join();

		BOOST_LOG_TRIVIAL(debug) << "generated exclusion map in " << time.elapsed() << " s";
	}

	void compute ( unsigned int beg, unsigned int end ) {
		vector<C> seq;
		seq.resize(this->s.high + 1);

		for ( unsigned int i = beg; i < end; ++i ) {
	    	seq[0] = point(i);
			data[i] = (base::evaluate(seq) == -1);
	    }

	    unsigned int refined = 0;
	    // recompute the borders horizontally
	    for ( unsigned int i = beg; i < end - 1; ++i ) {
			if ( !data[i] && data[i+1] ) { refine(seq, i+1,refined); };
	    }

	    for ( unsigned int i = end - 1; i > 0; --i ) {
			if ( data[i-1] && !data[i] ) { refine(seq, i-1, refined); };
	    }

	    BOOST_LOG_TRIVIAL(debug) << "refined " << refined << " border points";
	}

	void refine ( vector<C>& seq, unsigned int i, unsigned int& refined ) {
		unsigned int samples = 64;
		for ( unsigned int j = 0; j < samples; j++ ) {
			seq[0] = point(i) + C(uniform(generator), uniform(generator));
			if ( base::evaluate(seq) != -1 ) {
				data[i] = false;
				refined++;
				return;
			}
		}
	}

	inline unsigned int index ( unsigned int x, unsigned int y ) const {
		return y * size + x;
	}

	inline unsigned int index ( const C& c ) const {
		int x, y;
		index(c, x, y);
		return index(x, y); // unsafe
	}

	inline bool border ( const C& c ) const {
		int x, y;
		index(c, x, y);

		return border(x,y);
	}

	inline bool border ( int x, int y) {
		if ( !data[index(x,y)] ) return false;
		if ( x > 0 && !data[index(x-1,y)] ) return true;
		if ( y > 0 && !data[index(x,y-1)] ) return true;
		if ( x < int(size - 1) && !data[index(x+1,y)] ) return true;
		if ( y < int(size / 2 - 1) && !data[index(x,y+1)] ) return true;
		return false;
	}

	inline void index ( const C& c, int& x, int& y ) const {
		x = c.real() * size / 4.0 + size / 2.0;
		y = -fabs(c.imag()) * size / 4.0 + size / 2.0;
	}

	C point ( unsigned int i ) const {
		int x = i % size - int(size) / 2;
		int y = i / size - int(size) / 2;

		return C(x * 4.0 / size, y * 4.0 / size);
	}

	vector<unsigned int> border ( ) {
		vector<unsigned int> out;
		for (unsigned int i = 0; i < size * size / 2; ++i ) {
			int x = i % size;
			int y = i / size;
			if ( border(x, y) ) out.emplace_back(i);
		}

		return out;
	}

	double radius ( ) const {
		return 4.0 / size;
	}

	bool load ( ) {
		if ( !boost::filesystem::exists( this->s.exclusion ) ) {
			BOOST_LOG_TRIVIAL(debug) << "unable to load exclusion map from '" << this->s.exclusion << "' (no file)";
			return false;
		}

		std::ifstream iss( this->s.exclusion, ios::in | ios::binary);
		bio::filtering_stream<bio::input> f;
		f.push(bio::gzip_decompressor());
		f.push(iss);
		bar::binary_iarchive ia(f);
		
    	BOOST_LOG_TRIVIAL(debug) << "loading " << size << "x" << size << " exclusion map";
    	timer time;
    	uint64_t saved;
    	ia >> saved;
    	if (saved != size * size / 2) {
			BOOST_LOG_TRIVIAL(debug) << "unable to load exclusion map from '" << this->s.exclusion << "' (different size)";
			return false;
    	}
		ia >> data;

    	BOOST_LOG_TRIVIAL(debug) << "successfully loaded exclusion map in " << time.elapsed() << " s";
		return true;
	}

	void save ( ) {
		//if ( boost::filesystem::exists( this->s.exclusion ) )
		//	return;

		std::ofstream oss( this->s.exclusion, std::ios::binary);
		bio::filtering_stream<bio::output> f;
		f.push(bio::gzip_compressor());
		f.push(oss);
		bar::binary_oarchive oa(f);

    	BOOST_LOG_TRIVIAL(debug) << "saving " << size << "x" << size << " exclusion map";
    	timer time;
    	uint64_t saved = size * size / 2;
    	oa << saved;
		oa << data;


		rgb8_image_t img(size, size);
		rgb8_image_t::view_t v = view(img);

		/*for ( uint64_t i = 0; i < size * size / 2; ++i ) {
			int x = i % size;
			int y = i / size;
			if ( data[i] ) v(x, y) = v(x, size-y-1) = rgb8_pixel_t(255,255,255);
			else v(x,y) = v(x, size-y-1) = rgb8_pixel_t(0,0,0);
		}

		png_write_view("exclusion.png", const_view(img));*/


    	BOOST_LOG_TRIVIAL(debug) << "successfully saved exclusion map in " << time.elapsed() << " s";
	}

	inline bool excluded( const C& c ) const {
		int x, y;
		index(c, x, y);;
		if ( x < 0 || x >= int(size) || y < 0 || y >= int(size/2) ) return false;

		return data[index(x, y)];
	}











	int evaluate ( vector<C>& seq ) const {
	    //if ( excluded(seq[0]) )
	    //	return -1;

		return base::evaluate( seq );
	}

	int evaluate ( vector<C>& seq, unsigned int& calculated ) const {
	    if ( excluded(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

		return base::evaluate( seq, calculated );
	}

	int evaluate ( vector<C>& seq, unsigned int& contribute, unsigned int& calculated ) const {
	    if ( excluded(seq[0]) ) {
	    	calculated = 0;
	    	return -1;
	    }

		return base::evaluate( seq, contribute, calculated );
	}
};



#endif