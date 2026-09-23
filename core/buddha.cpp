/*
 * Copyright (c) 2010, Emilio Del Tessandoro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY EMILIO DEL TESSANDORO o ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL EMILIO DEL TESSANDORO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#include "buddha_generator.h"
#include "saver.h"


#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <zstd.h>
#include "timer.h"

#include <streambuf>

namespace bar = boost::archive;

namespace {
void check_zstd(size_t result, const string& action) {
    if (ZSTD_isError(result))
        throw runtime_error(action + ": " + ZSTD_getErrorName(result));
}

class zstd_output_streambuf : public std::streambuf {
public:
    zstd_output_streambuf(const string& filename, uint32_t threads)
        : output_(filename, ios::binary | ios::trunc),
          context_(ZSTD_createCStream()),
          output_buffer_(ZSTD_CStreamOutSize()),
          finished_(false) {
        if (!output_)
            throw runtime_error("unable to open checkpoint output: " + filename);
        if (!context_)
            throw runtime_error("unable to create zstd compression context");

        try {
            check_zstd(ZSTD_CCtx_setParameter(context_, ZSTD_c_compressionLevel, 3),
                       "unable to configure zstd compression level");
            if (threads != 0)
                check_zstd(ZSTD_CCtx_setParameter(context_, ZSTD_c_nbWorkers, threads),
                           "unable to configure zstd compression workers");
        } catch (...) {
            ZSTD_freeCStream(context_);
            context_ = nullptr;
            throw;
        }
    }

    ~zstd_output_streambuf() {
        ZSTD_freeCStream(context_);
    }

    void finish() {
        if (finished_)
            return;

        ZSTD_inBuffer input = { nullptr, 0, 0 };
        size_t remaining = 0;
        do {
            ZSTD_outBuffer output = { output_buffer_.data(), output_buffer_.size(), 0 };
            remaining = ZSTD_compressStream2(context_, &output, &input, ZSTD_e_end);
            check_zstd(remaining, "unable to finish zstd checkpoint");
            write_output(output);
        } while (remaining != 0);

        output_.flush();
        if (!output_)
            throw runtime_error("unable to write zstd checkpoint");
        finished_ = true;
    }

protected:
    streamsize xsputn(const char* source, streamsize count) override {
        if (count <= 0)
            return 0;

        ZSTD_inBuffer input = { source, static_cast<size_t>(count), 0 };
        while (input.pos != input.size) {
            ZSTD_outBuffer output = { output_buffer_.data(), output_buffer_.size(), 0 };
            check_zstd(ZSTD_compressStream2(context_, &output, &input, ZSTD_e_continue),
                       "unable to compress zstd checkpoint");
            write_output(output);
        }
        return count;
    }

    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        const char value = traits_type::to_char_type(character);
        return xsputn(&value, 1) == 1 ? character : traits_type::eof();
    }

private:
    void write_output(const ZSTD_outBuffer& buffer) {
        if (buffer.pos == 0)
            return;
        output_.write(static_cast<const char*>(buffer.dst), static_cast<streamsize>(buffer.pos));
        if (!output_)
            throw runtime_error("unable to write zstd checkpoint");
    }

    ofstream output_;
    ZSTD_CStream* context_;
    vector<char> output_buffer_;
    bool finished_;
};

class zstd_input_streambuf : public std::streambuf {
public:
    explicit zstd_input_streambuf(const string& filename)
        : input_file_(filename, ios::binary),
          context_(ZSTD_createDStream()),
          input_buffer_(ZSTD_DStreamInSize()),
          output_buffer_(ZSTD_DStreamOutSize()),
          input_({ nullptr, 0, 0 }),
          input_eof_(false) {
        if (!input_file_)
            throw runtime_error("unable to open zstd checkpoint: " + filename);
        if (!context_)
            throw runtime_error("unable to create zstd decompression context");
        check_zstd(ZSTD_initDStream(context_), "unable to initialize zstd decompression");
        setg(output_buffer_.data(), output_buffer_.data(), output_buffer_.data());
    }

    ~zstd_input_streambuf() {
        ZSTD_freeDStream(context_);
    }

protected:
    int_type underflow() override {
        if (gptr() != egptr())
            return traits_type::to_int_type(*gptr());

        for (;;) {
            if (input_.pos == input_.size && !input_eof_) {
                input_file_.read(input_buffer_.data(), static_cast<streamsize>(input_buffer_.size()));
                const streamsize count = input_file_.gcount();
                if (count == 0) {
                    if (input_file_.bad())
                        throw runtime_error("unable to read zstd checkpoint");
                    input_eof_ = true;
                }
                input_ = { input_buffer_.data(), static_cast<size_t>(count), 0 };
            }

            ZSTD_outBuffer output = { output_buffer_.data(), output_buffer_.size(), 0 };
            const size_t remaining = ZSTD_decompressStream(context_, &output, &input_);
            check_zstd(remaining, "unable to decompress zstd checkpoint");

            if (output.pos != 0) {
                char* begin = output_buffer_.data();
                setg(begin, begin, begin + output.pos);
                return traits_type::to_int_type(*gptr());
            }

            if (input_eof_ && input_.pos == input_.size) {
                if (remaining != 0)
                    throw runtime_error("truncated zstd checkpoint");
                return traits_type::eof();
            }
        }
    }

private:
    ifstream input_file_;
    ZSTD_DStream* context_;
    vector<char> input_buffer_;
    vector<char> output_buffer_;
    ZSTD_inBuffer input_;
    bool input_eof_;
};

const size_t checkpoint_block_pixels = 1 << 20;

template <typename Archive>
void save_histogram(Archive& archive, const buddha::vector_type& raw) {
    vector<buddha::pixel> values(checkpoint_block_pixels);
    for (size_t offset = 0; offset < raw.size();) {
        size_t count = raw.size() - offset;
        if (count > values.size())
            count = values.size();
        for (size_t i = 0; i < count; ++i)
            values[i] = raw[offset + i].load();
        archive.save_binary(values.data(), count * sizeof(buddha::pixel));
        offset += count;
    }
}

uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint64_t generator_seed(uint64_t base_seed, uint64_t stream) {
    if (base_seed != 0)
        return splitmix64(base_seed + stream);

    random_device random;
    return (uint64_t(random()) << 32) | random();
}
}




buddha::buddha( const settings& s ) : core(s), s(s), computed(0) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::buddha()";

    s.dump( );

    raw.reserve(3 * s.size);
    for ( uint64_t i = 0; i < 3 * s.size; ++i )
        raw.emplace_back(0);

    for ( unsigned int i = 0; i < s.threads; ++i )
        generators.push_back( new buddha_generator( core, raw, s, generator_seed(s.seed, i) ) );

    clearBuffers();
    if ( s.exclusion != "" ) core.load();
    if ( s.infile != "" ) load( );
}




void buddha::reduce ( ) {
    //swap(raw, generators[0]->raw);

    unsigned long long find_attempts = 0;
    unsigned long long proposals = 0;
    unsigned long long accepted = 0;
    unsigned long long drawn_orbits = 0;
    for ( auto i : generators ) {
        computed += i->computed;
        find_attempts += i->find_attempts;
        proposals += i->proposals;
        accepted += i->accepted;
        drawn_orbits += i->drawn_orbits;
    }

    unsigned long long int total = 0;
    for ( auto i : raw ) total += i.load();

    
    BOOST_LOG_TRIVIAL(info) << "computed " << computed / 1000000.0 << " Mpoints in " 
                            << totaltime << " s (" << computed / totaltime / 1000000.0 << " Mpoints/s)";

    if (s.infile.empty())
        BOOST_LOG_TRIVIAL(info) << total / 1000000.0 << " Mpoints in the histogram ("
                                << total / totaltime / 1000000.0 << " Mpoints/s)";
    else
        BOOST_LOG_TRIVIAL(info) << total / 1000000.0 << " Mpoints in the histogram";
    BOOST_LOG_TRIVIAL(info) << "find attempts: " << find_attempts
                            << ", proposals: " << proposals
                            << ", accepted: " << accepted
                            << " (" << (proposals ? double(accepted) / proposals : 0.0) << ")"
                            << ", drawn orbits: " << drawn_orbits;
}




void buddha::save () {
    //BOOST_LOG_TRIVIAL(debug) << "buddha::save()";
    BOOST_LOG_TRIVIAL(info) << "buddha::save(), checkpoint format: zstd"
                            << ", compression threads: " << s.threads;
    reduce();
    timer time;

    // Save to a sibling temporary file, then atomically replace the checkpoint
    // once compression has completed.  This makes in-place --load resumes safe
    // against an interrupted or failed checkpoint write.
    const string checkpoint = s.outfile + ".zst";
    const string checkpoint_tmp = checkpoint + ".tmp";
    zstd_output_streambuf compressed(checkpoint_tmp, s.threads);
    {
        ostream oss(&compressed);
        bar::binary_oarchive oa(oss);
        save_histogram(oa, raw);
    }
    compressed.finish();
    if (std::rename(checkpoint_tmp.c_str(), checkpoint.c_str()) != 0) {
        throw runtime_error("unable to replace checkpoint output: " + checkpoint);
    }

    BOOST_LOG_TRIVIAL(info) << "buddha::save(), compression: " << time.elapsed() << " s";

    if (s.no_image) {
        BOOST_LOG_TRIVIAL(info) << "buddha::save(), image output skipped";
        return;
    }

    time.restart();

    write_tiff(this, &s, s.outfile + ".tiff");
    BOOST_LOG_TRIVIAL(info) << "buddha::save(), tiff: " << time.elapsed() << " s";
}


void buddha::load ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::load()";
    timer time;
    zstd_input_streambuf compressed(s.infile);
    istream iss(&compressed);
    bar::binary_iarchive ia(iss);
    for ( uint64_t i = 0; i < 3 * s.size; ++i ) {
        pixel current;
        ia >> current;
        raw[i].store(current);
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha::load(), decompression: " << time.elapsed() << " s";
}


buddha::~buddha ( ) {
}




void buddha::clearBuffers ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::clearBuffers()";
}


void buddha::startGenerators ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::startGenerators()";

    // Block all signals for background s.threads
    sigset_t new_mask;
    sigfillset(&new_mask);
    sigset_t old_mask;
    pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

    for ( unsigned int i = 0; i < s.threads; ++i ) {
        generators[i]->start( );
    }

    // Restore previous signals.
    pthread_sigmask(SIG_SETMASK, &old_mask, 0);
}


// stop the generators if they're running and if their status is different from STOP.
// XXX this can cause problems if a generator is in PAUSE, but for how the program is designed
// I think this is impossible.
// If the s.threads were running acquire completely the semaphore.
void buddha::stopGenerators ( ) {
    for ( unsigned int i = 0; i < s.threads; ++i ) {
        lock_guard<mutex> locker ( generators[i]->execution );
        generators[i]->finish = true;
    }

    for ( unsigned int i = 0; i < s.threads; ++i ) {
        generators[i]->t.join();
    }

    BOOST_LOG_TRIVIAL(debug) << "buddha::stopGenerators()";
}


void buddha::run ( ) {
    BOOST_LOG_TRIVIAL(debug) << "buddha::run()";

    timer time;
    startGenerators();

    // Wait for signal indicating time to shut down.
    sigset_t wait_mask;
    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGINT);
    sigaddset(&wait_mask, SIGQUIT);
    sigaddset(&wait_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
    int sig = 0;
    sigwait(&wait_mask, &sig);

    BOOST_LOG_TRIVIAL(debug) << "interrupt signal (" << sig << ") received";

    stopGenerators( );
    totaltime = time.elapsed();

    if ( getenv("BUDDHA_BENCHMARK_NO_SAVE") ) {
        reduce();
        return;
    }

    save( );
}
