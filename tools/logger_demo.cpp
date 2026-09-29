// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT
//
// Placeholder tool: parses command line with cxxopts and prints the library version.

#include <cstdio>

#include <cxxopts.hpp>

#include <turboq_logger/Logger.h>

int main(int argc, char** argv) {
    cxxopts::Options options("logger_demo", "turboq-logger demo tool");
    options.add_options()("h,help", "Print usage");

    auto const args = options.parse(argc, argv);
    if (args.count("help")) {
        std::puts(options.help().c_str());
        return 0;
    }

    auto const version = turboq_logger::version();
    std::printf("turboq-logger %.*s\n", static_cast<int>(version.size()), version.data());
    return 0;
}
