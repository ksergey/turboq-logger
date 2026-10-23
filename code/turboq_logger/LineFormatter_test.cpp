// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "LineFormatter.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include <doctest/doctest.h>
#include <fmt/std.h>

namespace turboq::logger::testing {
namespace {

// Local time is UTC in these tests
[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

constexpr std::time_t k20261023_102405 = 1792751045; // 2026-10-23 10:24:05 UTC

constexpr auto kLocation = std::source_location::current();

auto formatLine(LineFormatter& formatter, ::timespec const& timestamp, std::string_view message = "hello",
    LogLevel level = LogLevel::Info) -> std::string {
    fmt::memory_buffer out;
    formatter.format(out, kLocation, level, timestamp, std::this_thread::get_id(), message);
    return fmt::to_string(out);
}

/// The fields after the timestamp, as formatted for kLocation and this thread
auto suffix(std::string_view level, std::string_view message) -> std::string {
    return fmt::format(
        " {:<7} [{}] LineFormatter_test.cpp:{} {}\n", level, std::this_thread::get_id(), kLocation.line(), message);
}

} // namespace

TEST_SUITE("LineFormatter") {

    TEST_CASE("a line: local date and time with nanoseconds, level, thread, file:line, message") {
        LineFormatter formatter;
        auto const line = formatLine(formatter, {.tv_sec = k20261023_102405, .tv_nsec = 123});
        CHECK_EQ(line, "2026-10-23 10:24:05.000000123" + suffix("info", "hello"));
    }

    TEST_CASE("levels are padded to the longest name, so messages line up") {
        LineFormatter formatter;
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 0};
        CHECK_EQ(
            formatLine(formatter, ts, "m", LogLevel::Error), "2026-10-23 10:24:05.000000000" + suffix("error", "m"));
        CHECK_EQ(formatLine(formatter, ts, "m", LogLevel::Warning),
            "2026-10-23 10:24:05.000000000" + suffix("warning", "m"));
        CHECK_EQ(formatLine(formatter, ts, "m", LogLevel::TraceL3),
            "2026-10-23 10:24:05.000000000" + suffix("tracel3", "m"));
    }

    TEST_CASE("the cached second is refreshed when the second changes, either way") {
        LineFormatter formatter;
        CHECK(formatLine(formatter, {.tv_sec = k20261023_102405, .tv_nsec = 1})
                  .starts_with("2026-10-23 10:24:05.000000001"));
        CHECK(formatLine(formatter, {.tv_sec = k20261023_102405, .tv_nsec = 999999999})
                  .starts_with("2026-10-23 10:24:05.999999999"));
        CHECK(formatLine(formatter, {.tv_sec = k20261023_102405 + 1, .tv_nsec = 0})
                  .starts_with("2026-10-23 10:24:06.000000000"));
        CHECK(formatLine(formatter, {.tv_sec = k20261023_102405 - 1, .tv_nsec = 5}) // out of order
                  .starts_with("2026-10-23 10:24:04.000000005"));
        CHECK(formatLine(formatter, {.tv_sec = k20261023_102405 + 86400, .tv_nsec = 0})
                  .starts_with("2026-10-24 10:24:05.000000000"));
    }

    TEST_CASE("a message is copied as is, and the line always ends with a single newline") {
        LineFormatter formatter;
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 0};
        CHECK_EQ(formatLine(formatter, ts, ""), "2026-10-23 10:24:05.000000000" + suffix("info", ""));
        CHECK_EQ(formatLine(formatter, ts, "{} braces % and \t tab"),
            "2026-10-23 10:24:05.000000000" + suffix("info", "{} braces % and \t tab"));
    }

    TEST_CASE("lines are appended to the buffer") {
        LineFormatter formatter;
        static constexpr auto location = std::source_location::current();
        fmt::memory_buffer out;
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 0};
        formatter.format(out, location, LogLevel::Info, ts, std::thread::id{}, "first");
        formatter.format(out, location, LogLevel::Info, ts, std::thread::id{}, "second");
        auto const text = fmt::to_string(out);
        CHECK_EQ(std::count(text.begin(), text.end(), '\n'), 2);
        CHECK(text.find("first\n") != std::string::npos);
        CHECK(text.ends_with("second\n"));
    }

    TEST_CASE("baseName() keeps the file name of a path") {
        static_assert(LineFormatter::baseName("/a/b/c.cpp") == "c.cpp");
        static_assert(LineFormatter::baseName("c.cpp") == "c.cpp");
        static_assert(LineFormatter::baseName("dir/") == "");
        static_assert(LineFormatter::baseName("") == "");
    }
}

} // namespace turboq::logger::testing
