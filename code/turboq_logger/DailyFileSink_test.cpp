// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "DailyFileSink.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <unistd.h>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

namespace fs = std::filesystem;

// Local time is UTC in these tests, unless a test switches it (see TimeZone)
[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

/// Switches the local time zone for a scope
class TimeZone {
public:
    explicit TimeZone(char const* zone) {
        ::setenv("TZ", zone, 1);
        ::tzset();
    }
    ~TimeZone() {
        ::setenv("TZ", "UTC", 1);
        ::tzset();
    }
};

/// A fresh directory, removed with everything in it at the end of the scope
class TempDir {
private:
    fs::path path_;

public:
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "turboq-logger-test-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path_ = pattern;
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] auto path() const -> fs::path const& {
        return path_;
    }
};

/// time_t of a local date and time (in the current TZ)
auto localTime(int year, int month, int day, int hour = 12, int minute = 0, int second = 0) -> std::time_t {
    ::tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;
    return ::mktime(&local);
}

void log(DailyFileSink& sink, std::time_t time, std::string_view message) {
    static constexpr auto location = std::source_location::current();
    sink.write(location, LogLevel::Info, ::timespec{.tv_sec = time, .tv_nsec = 0}, std::this_thread::get_id(), message);
}

/// The messages in a log file: the text after the file:line field of each line
auto messagesIn(fs::path const& path) -> std::vector<std::string> {
    std::ifstream file{path};
    std::vector<std::string> messages;
    for (std::string line; std::getline(file, line);) {
        auto const at = line.find("DailyFileSink_test.cpp:");
        REQUIRE(at != std::string::npos);
        messages.push_back(line.substr(line.find(' ', at) + 1));
    }
    return messages;
}

/// Log files in dir, by file name
auto filesIn(fs::path const& dir) -> std::vector<std::string> {
    std::vector<std::string> names;
    for (auto const& entry : fs::directory_iterator{dir}) {
        names.push_back(entry.path().filename().string());
    }
    std::ranges::sort(names);
    return names;
}

auto todaysFile(fs::path const& prefix) -> fs::path {
    return DailyFileSink::filePath(prefix, std::time(nullptr));
}

} // namespace

TEST_SUITE("DailyFileSink") {

    TEST_CASE("file name: <prefix>_YYYYMMDD.log of the local day") {
        CHECK_EQ(DailyFileSink::filePath("logs/app", localTime(2026, 10, 23)), fs::path{"logs/app_20261023.log"});
        CHECK_EQ(DailyFileSink::filePath("app", localTime(2026, 1, 5, 0, 0, 0)), fs::path{"app_20260105.log"});
        CHECK_EQ(DailyFileSink::filePath("app", localTime(2026, 12, 31, 23, 59, 59)), fs::path{"app_20261231.log"});
        CHECK_EQ(DailyFileSink::filePath("app", localTime(2027, 1, 1, 0, 0, 0)), fs::path{"app_20270101.log"});

        TimeZone moscow{"Europe/Moscow"}; // UTC+3: 22:00 UTC is already the next day
        CHECK_EQ(DailyFileSink::filePath("app", 1792792800), fs::path{"app_20261024.log"}); // 2026-10-23 22:00 UTC
    }

    TEST_CASE("today's file is opened on construction, parent directories created") {
        TempDir dir;
        auto const prefix = dir.path() / "a" / "b" / "app";
        DailyFileSink sink{prefix};
        CHECK_EQ(sink.path(), todaysFile(prefix));
        CHECK(fs::exists(sink.path()));
        CHECK_EQ(fs::file_size(sink.path()), 0);
    }

    TEST_CASE("a file that can't be opened fails on construction") {
        TempDir dir;
        std::ofstream{dir.path() / "file"} << "not a directory";
        CHECK_THROWS_AS(DailyFileSink{dir.path() / "file" / "app"}, fs::filesystem_error);
        // fopen() itself fails: today's file name is taken by a directory
        fs::create_directory(todaysFile(dir.path() / "app"));
        CHECK_THROWS_AS(DailyFileSink{dir.path() / "app"}, std::system_error);
    }

    TEST_CASE("messages are written as lines, visible after flush()") {
        TempDir dir;
        DailyFileSink sink{dir.path() / "app"};
        auto const now = std::time(nullptr);
        log(sink, now, "first");
        log(sink, now, "second");
        sink.flush();
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{"first", "second"});
    }

    TEST_CASE("a new file starts at local midnight, from the message timestamps") {
        TempDir dir;
        auto const prefix = dir.path() / "app";
        DailyFileSink sink{prefix};

        log(sink, localTime(2030, 1, 1, 0, 0, 0), "day1 start");
        auto const day1 = sink.path();
        log(sink, localTime(2030, 1, 1, 23, 59, 59), "day1 end");
        CHECK_EQ(sink.path(), day1);
        log(sink, localTime(2030, 1, 2, 0, 0, 0), "day2 start");
        auto const day2 = sink.path();
        sink.flush();

        CHECK_EQ(day1, prefix.string() + "_20300101.log");
        CHECK_EQ(day2, prefix.string() + "_20300102.log");
        CHECK_EQ(messagesIn(day1), std::vector<std::string>{"day1 start", "day1 end"}); // closed, so flushed
        CHECK_EQ(messagesIn(day2), std::vector<std::string>{"day2 start"});
    }

    TEST_CASE("the file only moves forward: a late message of the previous day goes to the current file") {
        TempDir dir;
        auto const prefix = dir.path() / "app";
        DailyFileSink sink{prefix};

        log(sink, localTime(2030, 1, 1, 23, 59, 59), "day1");
        log(sink, localTime(2030, 1, 2, 0, 0, 1), "day2");
        log(sink, localTime(2030, 1, 1, 23, 59, 59), "day1, late");
        sink.flush();

        CHECK_EQ(sink.path(), prefix.string() + "_20300102.log");
        CHECK_EQ(messagesIn(prefix.string() + "_20300101.log"), std::vector<std::string>{"day1"});
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{"day2", "day1, late"});
    }

    TEST_CASE("a day without messages has no file") {
        TempDir dir;
        auto const prefix = dir.path() / "app";
        {
            DailyFileSink sink{prefix};
            log(sink, localTime(2030, 1, 1), "day1");
            log(sink, localTime(2030, 1, 4), "day4");
        }
        std::vector<std::string> expected{
            "app_20300101.log", "app_20300104.log", todaysFile(prefix).filename().string()};
        std::ranges::sort(expected); // today's file (from construction) is there too
        CHECK_EQ(filesIn(dir.path()), expected);
    }

    TEST_CASE("a restart the same day appends to the day's file") {
        TempDir dir;
        auto const prefix = dir.path() / "app";
        auto const time = localTime(2030, 6, 15);
        {
            DailyFileSink sink{prefix};
            log(sink, time, "before restart");
        }
        {
            DailyFileSink sink{prefix};
            log(sink, time + 60, "after restart");
        }
        CHECK_EQ(messagesIn(DailyFileSink::filePath(prefix, time)),
            std::vector<std::string>{"before restart", "after restart"});
    }

    TEST_CASE("days of 23 and 25 hours (DST) end at local midnight") {
        TimeZone berlin{"Europe/Berlin"}; // DST starts 2030-03-31, ends 2030-10-27
        TempDir dir;
        auto const prefix = dir.path() / "app";
        DailyFileSink sink{prefix};

        log(sink, localTime(2030, 3, 31, 0, 30), "spring start");
        log(sink, localTime(2030, 3, 31, 23, 59, 59), "spring end"); // 22 h 59 min later
        log(sink, localTime(2030, 4, 1, 0, 0, 0), "april");
        log(sink, localTime(2030, 10, 27, 0, 30), "autumn start");
        log(sink, localTime(2030, 10, 27, 23, 59, 59), "autumn end"); // 24 h 59 min later
        log(sink, localTime(2030, 10, 28, 0, 0, 0), "after autumn");
        sink.flush();

        CHECK_EQ(messagesIn(prefix.string() + "_20300331.log"), std::vector<std::string>{"spring start", "spring end"});
        CHECK_EQ(messagesIn(prefix.string() + "_20300401.log"), std::vector<std::string>{"april"});
        CHECK_EQ(messagesIn(prefix.string() + "_20301027.log"), std::vector<std::string>{"autumn start", "autumn end"});
        CHECK_EQ(messagesIn(prefix.string() + "_20301028.log"), std::vector<std::string>{"after autumn"});
    }

    TEST_CASE("fsyncOnFlush") {
        TempDir dir;
        DailyFileSink sink{dir.path() / "app", DailyFileSinkOptions{.bufferSize = 512, .fsyncOnFlush = true}};
        log(sink, std::time(nullptr), "synced");
        CHECK_NOTHROW(sink.flush());
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{"synced"});
    }

    TEST_CASE("a small buffer: lines longer than it are written whole") {
        TempDir dir;
        DailyFileSink sink{dir.path() / "app", DailyFileSinkOptions{.bufferSize = 16}};
        std::string const longMessage(1000, 'x');
        log(sink, std::time(nullptr), longMessage);
        log(sink, std::time(nullptr), "short");
        sink.flush();
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{longMessage, "short"});
    }
}

} // namespace turboq::logger::testing
