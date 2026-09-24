#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/serialization/vector.hpp>

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <vector>

namespace bar = boost::archive;
namespace bio = boost::iostreams;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: exclusion-convert INPUT.exclusion OUTPUT.exclusion.zst\n";
        return 2;
    }

    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            std::cerr << "unable to open input exclusion map: " << argv[1] << '\n';
            return 1;
        }

        uint64_t saved = 0;
        std::vector<bool> data;
        {
            bio::filtering_stream<bio::input> stream;
            stream.push(bio::gzip_decompressor());
            stream.push(input);
            bar::binary_iarchive archive(stream);
            archive >> saved;
            archive >> data;
        }

        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "unable to open output exclusion map: " << argv[2] << '\n';
            return 1;
        }
        {
            bio::filtering_stream<bio::output> stream;
            stream.push(bio::zstd_compressor());
            stream.push(output);
            bar::binary_oarchive archive(stream);
            archive << saved;
            archive << data;
        }

        std::cout << "converted " << saved << " exclusion entries to " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "conversion failed: " << error.what() << '\n';
        return 1;
    }
}
