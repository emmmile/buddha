#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <random>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/filesystem.hpp>
#include "timer.h"

#include "mandelbrot_base.h"
namespace bar = boost::archive;
namespace bio = boost::iostreams;
using namespace std;

template <class C> struct mandelbrot : public mandelbrot_base<C> {
    const uint64_t size;
    vector<uint8_t> data;

    typedef mandelbrot_base<C> base;
    mandelbrot(const settings &s) : base(s), size(s.exclusion_size) {
        if (size == 0 || size % 2 != 0)
            throw std::invalid_argument("exclusion map size must be a positive even number");
        data.resize(size * size / 2);
    }

    void exclusion() {
        // compute the exclusion map
        BOOST_LOG_TRIVIAL(debug) << "generating " << size << "x" << size << " exclusion map";

        timer time;

        const size_t count = data.size();
        const size_t workers = std::min<size_t>(std::max(1u, this->s.threads), count);
        vector<thread> threads;
        for (size_t t = 0; t < workers; ++t)
            threads.emplace_back(&mandelbrot::compute, this, t * count / workers,
                                 (t + 1) * count / workers);

        for (auto &worker : threads)
            worker.join();

        const vector<uint8_t> initial = data;
        threads.clear();
        for (size_t t = 0; t < workers; ++t)
            threads.emplace_back(&mandelbrot::refine_range, this, std::cref(initial),
                                 t * count / workers, (t + 1) * count / workers, t);
        for (auto &worker : threads)
            worker.join();

        BOOST_LOG_TRIVIAL(debug) << "generated exclusion map in " << time.elapsed() << " s";
    }

    void compute(size_t beg, size_t end) {
        vector<C> seq;
        seq.resize(this->s.high + 1);

        for (size_t i = beg; i < end; ++i) {
            seq[0] = point(i);
            data[i] = (base::evaluate(seq) == -1);
        }
    }

    void refine_range(const vector<uint8_t> &initial, size_t beg, size_t end, size_t worker_id) {
        vector<C> seq(this->s.high + 1);
        std::random_device random;
        std::mt19937_64 generator((uint64_t(random()) << 32) ^ random() ^ worker_id);
        std::uniform_real_distribution<double> uniform(0, 4.0 / size);
        unsigned int refined = 0;
        for (size_t i = beg; i < end; ++i) {
            const size_t x = i % size;
            if (initial[i] && ((x > 0 && !initial[i - 1]) || (x + 1 < size && !initial[i + 1])))
                refine(seq, i, refined, generator, uniform);
        }

        BOOST_LOG_TRIVIAL(debug) << "refined " << refined << " border points";
    }

    void refine(vector<C> &seq, size_t i, unsigned int &refined, std::mt19937_64 &generator,
                std::uniform_real_distribution<double> &uniform) {
        unsigned int samples = 64;
        for (unsigned int j = 0; j < samples; j++) {
            seq[0] = point(i) + C(uniform(generator), uniform(generator));
            if (base::evaluate(seq) != -1) {
                data[i] = false;
                refined++;
                return;
            }
        }
    }

    inline unsigned int index(unsigned int x, unsigned int y) const { return y * size + x; }

    inline unsigned int index(const C &c) const {
        int x, y;
        index(c, x, y);
        return index(x, y); // unsafe
    }

    inline bool border(const C &c) const {
        int x, y;
        index(c, x, y);

        return border(x, y);
    }

    inline bool border(int x, int y) {
        if (!data[index(x, y)])
            return false;
        if (x > 0 && !data[index(x - 1, y)])
            return true;
        if (y > 0 && !data[index(x, y - 1)])
            return true;
        if (x < int(size - 1) && !data[index(x + 1, y)])
            return true;
        if (y < int(size / 2 - 1) && !data[index(x, y + 1)])
            return true;
        return false;
    }

    inline void index(const C &c, int &x, int &y) const {
        x = c.real() * size / 4.0 + size / 2.0;
        y = -fabs(c.imag()) * size / 4.0 + size / 2.0;
    }

    C point(size_t i) const {
        int x = static_cast<int>(i % size) - static_cast<int>(size) / 2;
        int y = static_cast<int>(i / size) - static_cast<int>(size) / 2;

        return C(x * 4.0 / size, y * 4.0 / size);
    }

    vector<unsigned int> border() {
        vector<unsigned int> out;
        for (unsigned int i = 0; i < size * size / 2; ++i) {
            int x = i % size;
            int y = i / size;
            if (border(x, y))
                out.emplace_back(i);
        }

        return out;
    }

    double radius() const { return 4.0 / size; }

    bool load() {
        if (!boost::filesystem::exists(this->s.exclusion)) {
            BOOST_LOG_TRIVIAL(debug)
                << "unable to load exclusion map from '" << this->s.exclusion << "' (no file)";
            return false;
        }

        std::ifstream iss(this->s.exclusion, ios::in | ios::binary);
        bio::filtering_stream<bio::input> f;
        f.push(bio::zstd_decompressor());
        f.push(iss);
        bar::binary_iarchive ia(f);

        BOOST_LOG_TRIVIAL(debug) << "loading " << size << "x" << size << " exclusion map";
        timer time;
        uint64_t saved;
        ia >> saved;
        if (saved != size * size / 2) {
            BOOST_LOG_TRIVIAL(debug) << "unable to load exclusion map from '" << this->s.exclusion
                                     << "' (different size)";
            return false;
        }
        vector<bool> packed;
        ia >> packed;
        if (packed.size() != data.size())
            throw std::runtime_error("invalid exclusion map size");
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = packed[i];

        BOOST_LOG_TRIVIAL(debug) << "successfully loaded exclusion map in " << time.elapsed()
                                 << " s";
        return true;
    }

    void save() {
        // if ( boost::filesystem::exists( this->s.exclusion ) )
        //	return;

        std::ofstream oss(this->s.exclusion, std::ios::binary);
        bio::filtering_stream<bio::output> f;
        f.push(bio::zstd_compressor());
        f.push(oss);
        bar::binary_oarchive oa(f);

        BOOST_LOG_TRIVIAL(debug) << "saving " << size << "x" << size << " exclusion map";
        timer time;
        uint64_t saved = size * size / 2;
        oa << saved;
        vector<bool> packed(data.size());
        for (size_t i = 0; i < data.size(); ++i)
            packed[i] = data[i];
        oa << packed;

        BOOST_LOG_TRIVIAL(debug) << "successfully saved exclusion map in " << time.elapsed()
                                 << " s";
    }

    inline bool excluded(const C &c) const {
        int x, y;
        index(c, x, y);
        ;
        if (x < 0 || x >= int(size) || y < 0 || y >= int(size / 2))
            return false;

        return data[index(x, y)];
    }

    int evaluate(vector<C> &seq) const {
        // if ( excluded(seq[0]) )
        //	return -1;

        return base::evaluate(seq);
    }

    int evaluate(vector<C> &seq, unsigned int &calculated) const {
        if (excluded(seq[0])) {
            calculated = 0;
            return -1;
        }

        return base::evaluate(seq, calculated);
    }

    int evaluate(vector<C> &seq, unsigned int &contribute, unsigned int &calculated) const {
        if (excluded(seq[0])) {
            contribute = 0;
            calculated = 0;
            return -1;
        }

        return base::evaluate(seq, contribute, calculated);
    }
};

#endif
