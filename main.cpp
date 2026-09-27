
#include "core/buddha.h"
#include "core/settings_parser.h"

#include <iostream>

int main(int argc, char **argv) {
    try {
        settings_parser parser(argc, argv);
        buddha b(parser());
        b.run();
    } catch (const std::exception &error) {
        std::cerr << "buddha++: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
