// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdio>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <turboq_logger/Logging.h>

/// Sink prints to stdout
class StdOutSink final : public turboq::logger::Sink {
    void write([[maybe_unused]] std::source_location const& source, turboq::logger::LogLevel level,
        ::timespec const& timestamp, std::thread::id const& threadID, std::string_view message) override {
        fmt::print(stdout, "{}.{:09} [{}] {} {}\n", timestamp.tv_sec, timestamp.tv_nsec, logLevelToString(level),
            threadID, message);
    }

    void flush() override {}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    try {
        turboq::logger::BackendThread::setSink(std::make_shared<StdOutSink>());
        turboq::logger::BackendThread::start();

        tq_info("hello {}", "world");
        tq_trace("Point at {}.{}", 10, 15.0);
        tq_trace_l2("Done");

        turboq::logger::BackendThread::stop();

    } catch (std::exception const& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
