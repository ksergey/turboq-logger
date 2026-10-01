// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "DailyFileSink.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/std.h>

namespace turboq::logger::testing {
namespace {

/// A fresh temporary directory, removed with everything in it
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        auto pattern = (std::filesystem::temp_directory_path() / "turboq-logger-test-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path = pattern;
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

/// UTC time as a timespec
auto utc(int year, int month, int day, int hour, int min, int sec, long nsec = 0) -> ::timespec {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    return ::timespec{.tv_sec = ::timegm(&tm), .tv_nsec = nsec};
}

auto readFile(std::filesystem::path const& path) -> std::string {
    std::ifstream in{path};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The message of every line of a file (the last word: test messages are single words)
auto messagesIn(std::filesystem::path const& path) -> std::vector<std::string> {
    std::vector<std::string> messages;
    std::istringstream lines{readFile(path)};
    for (std::string line; std::getline(lines, line);) {
        messages.push_back(line.substr(line.rfind(' ') + 1));
    }
    return messages;
}

void write(Sink& sink, LogLevel level, ::timespec const& timestamp, std::string_view message,
    std::source_location const& location = std::source_location::current()) {
    sink.write(location, level, timestamp, std::this_thread::get_id(), message);
}

using Messages = std::vector<std::string>;

} // namespace

TEST_SUITE("DailyFileSink") {

    TEST_CASE("writes one formatted line per message") {
        TempDir dir;
        std::optional<DailyFileSink> sink{std::in_place, (dir.path / "app-{:%Y-%m-%d}.log").string(), TimeZone::UTC};
        auto const location = std::source_location::current();
        sink->write(location, LogLevel::Notice, utc(2026, 10, 1, 12, 34, 56, 123), std::this_thread::get_id(), "hello world");
        CHECK_EQ(sink->path(), dir.path / "app-2026-10-01.log");
        sink.reset();

        CHECK_EQ(readFile(dir.path / "app-2026-10-01.log"),
            fmt::format("2026-10-01 12:34:56.000000123 NOTICE [{}] DailyFileSink_test.cpp:{} hello world\n",
                std::this_thread::get_id(), location.line()));
    }

    TEST_CASE("starts a new file every day, each beginning with all Always messages so far") {
        TempDir dir;
        auto const file = [&](std::string_view day) {
            return dir.path / "logs" / fmt::format("app-{}.log", day); // logs/ is created
        };
        {
            DailyFileSink sink{(dir.path / "logs/app-{:%Y-%m-%d}.log").string(), TimeZone::UTC};
            write(sink, LogLevel::Always, utc(2026, 10, 1, 10, 0, 0), "banner");
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 11, 0, 0), "a");
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 23, 59, 59), "b");
            write(sink, LogLevel::Notice, utc(2026, 10, 2, 0, 0, 0), "c");          // midnight: next file
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 23, 59, 59, 500), "late"); // stays in the current file
            write(sink, LogLevel::Always, utc(2026, 10, 2, 9, 0, 0), "config");
            write(sink, LogLevel::Notice, utc(2026, 10, 4, 8, 0, 0), "d");          // a day without messages
        }

        CHECK_EQ(messagesIn(file("2026-10-01")), Messages{"banner", "a", "b"});
        CHECK_EQ(messagesIn(file("2026-10-02")), Messages{"banner", "c", "late", "config"});
        CHECK_FALSE(std::filesystem::exists(file("2026-10-03")));
        CHECK_EQ(messagesIn(file("2026-10-04")), Messages{"banner", "config", "d"});
    }

    TEST_CASE("a restarted sink appends to today's file") {
        TempDir dir;
        auto const pattern = (dir.path / "app-{:%Y%m%d}.log").string();
        {
            DailyFileSink sink{pattern, TimeZone::UTC};
            write(sink, LogLevel::Always, utc(2026, 10, 1, 9, 0, 0), "first");
        }
        {
            DailyFileSink sink{pattern, TimeZone::UTC};
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 10, 0, 0), "second");
        }
        // the second sink knows no Always message yet: nothing replayed
        CHECK_EQ(messagesIn(dir.path / "app-20261001.log"), Messages{"first", "second"});
    }

    TEST_CASE("flush() puts the lines on disk while the sink is alive") {
        TempDir dir;
        DailyFileSink sink{(dir.path / "app-{:%Y-%m-%d}.log").string(), TimeZone::UTC};
        sink.flush(); // nothing open yet: fine
        write(sink, LogLevel::Notice, utc(2026, 10, 1, 12, 0, 0), "flushed");
        sink.flush();
        CHECK_EQ(messagesIn(sink.path()), Messages{"flushed"});
    }

    TEST_CASE("days follow the local time zone by default") {
        // POSIX TZ string: no tz database needed. JST-9 is UTC+9.
        auto const* const previous = std::getenv("TZ");
        auto const saved = previous != nullptr ? std::optional<std::string>{previous} : std::nullopt;
        ::setenv("TZ", "JST-9", 1);
        ::tzset();

        TempDir dir;
        {
            DailyFileSink sink{(dir.path / "app-{:%Y-%m-%d}.log").string()};
            CHECK_EQ(sink.timeZone(), TimeZone::Local);
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 14, 0, 0), "a"); // 23:00 local, Oct 1
            write(sink, LogLevel::Notice, utc(2026, 10, 1, 15, 0, 0), "b"); // 00:00 local, Oct 2
        }
        CHECK_EQ(messagesIn(dir.path / "app-2026-10-01.log"), Messages{"a"});
        auto const day2 = readFile(dir.path / "app-2026-10-02.log");
        CHECK(day2.starts_with("2026-10-02 00:00:00.000000000 NOTICE")); // timestamps in local time too

        if (saved) {
            ::setenv("TZ", saved->c_str(), 1);
        } else {
            ::unsetenv("TZ");
        }
        ::tzset();
    }

    TEST_CASE("an invalid file name pattern is rejected on construction") {
        CHECK_THROWS_AS(DailyFileSink{"app-{:%Y-%m-%d.log"}, std::invalid_argument);
    }
}

} // namespace turboq::logger::testing
