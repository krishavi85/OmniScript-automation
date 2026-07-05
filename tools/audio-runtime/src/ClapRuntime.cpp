#include <clap/clap.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: OmniStemClapRuntime --scan|--test plugin.clap\n";
        return 64;
    }
    return runClapRuntime(argv[1], argv[2]);
}
