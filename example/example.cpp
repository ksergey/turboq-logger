// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

// Usage: example [file <path> | daily <prefix>]
//   no argument:     messages go to stdout
//   file <path>:     messages go to the file <path>
//   daily <prefix>:  messages go to <prefix>_YYYYMMDD.log, a new file each day

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>

#include <turboq-logger/DailyFileSink.h>
#include <turboq-logger/FileSink.h>
#include <turboq-logger/Logging.h>
#include <turboq-logger/StdOutSink.h>

int main(int argc, char* argv[]) {
    using namespace turboq::logger;

    try {
        auto const mode = argc > 2 ? std::string_view{argv[1]} : std::string_view{};
        if (mode == "file") {
            BackendThread::setSink(std::make_shared<FileSink>(argv[2]));
        } else if (mode == "daily") {
            // a pattern of our own, checked at compile time
            BackendThread::setSink(std::make_shared<DailyFileSink>(
                argv[2], makeFormatter<"{time:%T}.{us:06} {level:<7} {file}:{line} {message}">()));
        } else {
            BackendThread::setSink(std::make_shared<StdOutSink>());
        }
        BackendThread::start();

        tq_info("hello {}", "world");
        tq_trace("Point at {}.{}", 10, 15.0);
        tq_trace_l2("Done");

        tq_error("Error");
        tq_warn("Warnig");
        tq_notice("Notice");
        tq_info("Info");
        tq_debug("Debug");

        BackendThread::stop();

    } catch (std::exception const& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
