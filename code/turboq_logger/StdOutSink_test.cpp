// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "StdOutSink.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <unistd.h>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

constexpr std::time_t k20261023_102405 = 1792751045; // 2026-10-23 10:24:05 UTC

/// Return what fn() writes to stdout
template <typename Fn>
auto captureStdout(Fn&& fn) -> std::string {
    std::fflush(stdout);
    auto* const file = std::tmpfile();
    REQUIRE(file != nullptr);
    auto const saved = ::dup(STDOUT_FILENO);
    ::dup2(::fileno(file), STDOUT_FILENO);

    fn();

    std::fflush(stdout);
    ::dup2(saved, STDOUT_FILENO);
    ::close(saved);

    std::string output;
    std::rewind(file);
    for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
        output.push_back(static_cast<char>(c));
    }
    std::fclose(file);
    return output;
}

} // namespace

TEST_SUITE("StdOutSink") {

    TEST_CASE("messages are written to stdout as LineFormatter lines") {
        static constexpr auto location = std::source_location::current();
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 42};

        LineFormatter formatter;
        fmt::memory_buffer expected;
        formatter.format(expected, location, LogLevel::Warning, ts, std::this_thread::get_id(), "first");
        formatter.format(expected, location, LogLevel::Info, ts, std::this_thread::get_id(), "second");

        StdOutSink sink;
        auto const output = captureStdout([&] {
            sink.write(location, LogLevel::Warning, ts, std::this_thread::get_id(), "first");
            sink.write(location, LogLevel::Info, ts, std::this_thread::get_id(), "second");
            sink.flush();
        });
        CHECK_EQ(output, fmt::to_string(expected));
        CHECK(output.starts_with("2026-10-23 10:24:05.000000042 warning "));
    }

    TEST_CASE("flush() with nothing written") {
        StdOutSink sink;
        CHECK_EQ(captureStdout([&] {
            sink.flush();
        }),
            "");
    }
}

} // namespace turboq::logger::testing
