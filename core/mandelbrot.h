#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <bit>
#include <cstring>
#include <random>
#include <boost/random/xoshiro.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/filesystem.hpp>
#include "timer.h"

#include "mandelbrot_base.h"
namespace bio = boost::iostreams;
using namespace std;

// Exclusion map file, zstd-compressed and little-endian:
//   "BUDDHAEX"         magic
//   uint32 version     exclusion_format_version
//   uint32 iterations  iteration limit the map was computed with
//   uint64 size        resolution: the map has size x size/2 cells
//   cells              one bit per cell, cell i in bit i % 8 of byte i / 8
constexpr char exclusion_magic[8] = {'B', 'U', 'D', 'D', 'H', 'A', 'E', 'X'};
constexpr uint32_t exclusion_format_version = 1;

template <class C> struct mandelbrot : public mandelbrot_base<C> {
    // Resolution and cells of the exclusion map; empty (size 0) excludes nothing.
    uint64_t size = 0;
    uint32_t iterations = 0;
    vector<uint8_t> data;

    typedef mandelbrot_base<C> base;
    mandelbrot(const settings &s) : base(s) {}

    // An all-outside map of the given resolution.
    void resize(uint64_t resolution) {
        if (resolution == 0 || resolution % 2 != 0 || resolution > 65536)
            throw std::invalid_argument("exclusion map size must be an even number in [2, 65536]");
        size = resolution;
        data.assign(size * size / 2, 0);
    }

    void exclusion(uint64_t resolution) {
        // compute the exclusion map
        resize(resolution);
        iterations = this->s.high;
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
        boost::random::xoshiro256pp generator((uint64_t(random()) << 32) ^ random() ^ worker_id);
        std::uniform_real_distribution<double> uniform(0, 4.0 / size);
        unsigned int refined = 0;
        for (size_t i = beg; i < end; ++i) {
            const size_t x = i % size;
            if (initial[i] && ((x > 0 && !initial[i - 1]) || (x + 1 < size && !initial[i + 1])))
                refine(seq, i, refined, generator, uniform);
        }

        BOOST_LOG_TRIVIAL(debug) << "refined " << refined << " border points";
    }

    void refine(vector<C> &seq, size_t i, unsigned int &refined,
                boost::random::xoshiro256pp &generator,
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
        buddha_kernel::exclusion_cell(c.real(), c.imag(), uint32_t(size), x, y);
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

        timer time;
        const auto invalid = [&](const string &why) {
            return std::runtime_error("invalid exclusion map '" + this->s.exclusion + "' (" + why +
                                      "); regenerate it with the exclusion tool");
        };
        char magic[sizeof exclusion_magic];
        uint32_t version = 0, computed = 0;
        uint64_t resolution = 0;
        if (!read(f, magic) || memcmp(magic, exclusion_magic, sizeof magic) != 0)
            throw invalid("not an exclusion map, or an old format without resolution");
        if (!read(f, version) || version != exclusion_format_version)
            throw invalid("unsupported version " + std::to_string(version));
        if (!read(f, computed) || !read(f, resolution))
            throw invalid("truncated header");
        try {
            resize(resolution);
        } catch (const std::invalid_argument &e) {
            throw invalid(e.what());
        }
        iterations = computed;

        vector<uint8_t> packed((data.size() + 7) / 8);
        if (!f.read(reinterpret_cast<char *>(packed.data()), std::streamsize(packed.size())))
            throw invalid("truncated cells");
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = (packed[i / 8] >> (i % 8)) & 1;

        BOOST_LOG_TRIVIAL(info) << "loaded " << size << "x" << size << " exclusion map ("
                                << iterations << " iterations) in " << time.elapsed() << " s";
        if (this->s.high > iterations)
            BOOST_LOG_TRIVIAL(warning) << "the exclusion map was computed with " << iterations
                                       << " iterations, fewer than the " << this->s.high
                                       << " rendered: it may exclude points that escape late";
        return true;
    }

    void save() const {
        std::ofstream oss(this->s.exclusion, std::ios::binary);
        bio::filtering_stream<bio::output> f;
        f.push(bio::zstd_compressor());
        f.push(oss);

        BOOST_LOG_TRIVIAL(debug) << "saving " << size << "x" << size << " exclusion map";
        timer time;
        vector<uint8_t> packed((data.size() + 7) / 8);
        for (size_t i = 0; i < data.size(); ++i)
            packed[i / 8] |= uint8_t((data[i] != 0) << (i % 8));
        f.write(exclusion_magic, sizeof exclusion_magic);
        write(f, exclusion_format_version);
        write(f, iterations);
        write(f, size);
        f.write(reinterpret_cast<const char *>(packed.data()), std::streamsize(packed.size()));
        f.reset();
        if (!oss)
            throw std::runtime_error("unable to save exclusion map to '" + this->s.exclusion + "'");

        BOOST_LOG_TRIVIAL(debug) << "successfully saved exclusion map in " << time.elapsed()
                                 << " s";
    }

    // Shared with buddha-metal (buddha_kernel.h).
    inline bool excluded(const C &c) const {
        return buddha_kernel::excluded(c.real(), c.imag(), uint32_t(size), data);
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
            calculated = 0;
            return -1;
        }

        return base::evaluate(seq, contribute, calculated);
    }

  private:
    static_assert(std::endian::native == std::endian::little, "exclusion maps are little-endian");

    template <class T> static bool read(std::istream &in, T &value) {
        return bool(in.read(reinterpret_cast<char *>(&value), sizeof value));
    }

    template <class T> static void write(std::ostream &out, const T &value) {
        out.write(reinterpret_cast<const char *>(&value), sizeof value);
    }
};

#endif
