// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Formatter.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include <doctest/doctest.h>
#include <fmt/std.h>

namespace turboq::logger::testing {
namespace {

// Local time is UTC in these tests, unless a test switches it (see TimeZone)
[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

constexpr std::time_t k20261023_102405 = 1792751045; // 2026-10-23 10:24:05 UTC
constexpr ::timespec kTimestamp{.tv_sec = k20261023_102405, .tv_nsec = 123456789};

constexpr auto kLocation = std::source_location::current();

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

auto formatLine(FormatterBase& formatter, ::timespec const& timestamp, std::string_view message = "hello",
    LogLevel level = LogLevel::Info) -> std::string {
    fmt::memory_buffer out;
    formatter.format(out, kLocation, level, timestamp, std::this_thread::get_id(), message);
    return fmt::to_string(out);
}

/// The line of one message with Pattern, without its '\n'
template <FixedString Pattern>
auto formatWith(std::string_view message = "hello", LogLevel level = LogLevel::Info,
    ::timespec const& timestamp = kTimestamp, ColorMode colors = ColorMode::Never) -> std::string {
    Formatter<Pattern> formatter{{.colors = colors}};
    auto line = formatLine(formatter, timestamp, message, level);
    REQUIRE(line.ends_with('\n'));
    line.pop_back();
    return line;
}

/// The fields of the default pattern after the timestamp, for kLocation and this thread
auto defaultSuffix(std::string_view level, std::string_view message) -> std::string {
    return fmt::format(
        " {:<7} [{}] Formatter_test.cpp:{} {}\n", level, std::this_thread::get_id(), kLocation.line(), message);
}

} // namespace

TEST_SUITE("Formatter translation") {

    TEST_CASE("names become positional arguments at compile time") {
        static_assert(
            detail::PatternInfo<kDefaultPattern>::translation.view() == "{10}.{0:09} {3:<7} [{4}] {5}:{7} {9}");
        static_assert(detail::PatternInfo<"{message}">::translation.view() == "{9}");
        static_assert(detail::PatternInfo<"{{{level}}} }}{{">::translation.view() == "{{{3}}} }}{{");
        static_assert(detail::PatternInfo<"{utc:%T} {time} {utc}">::translation.view() == "{10} {11} {12}");
        static_assert(detail::PatternInfo<"no fields">::translation.view() == "no fields");
        static_assert(detail::PatternInfo<"">::translation.view().empty());
    }

    TEST_CASE("time fields are collected with their spec") {
        constexpr auto const& translation = detail::PatternInfo<"{utc:%T} {time} {thread}">::translation;
        static_assert(translation.timeCount == 2);
        static_assert(translation.times[0].utc && translation.times[0].spec == "%T");
        static_assert(!translation.times[1].utc && translation.times[1].spec.empty());
        static_assert(translation.hasLocalTime && translation.hasUtcTime);
        static_assert(!detail::PatternInfo<"{message}">::translation.hasLocalTime);
    }

    TEST_CASE("pattern() is the pattern as written") {
        static_assert(DefaultFormatter::pattern() == kDefaultPattern.view());
        static_assert(Formatter<"{level} {message}">::pattern() == "{level} {message}");
    }
}

TEST_SUITE("Formatter default pattern") {

    TEST_CASE("a line: local date and time with nanoseconds, level, thread, file:line, message") {
        DefaultFormatter formatter;
        auto const line = formatLine(formatter, {.tv_sec = k20261023_102405, .tv_nsec = 123});
        CHECK_EQ(line, "2026-10-23 10:24:05.000000123" + defaultSuffix("info", "hello"));
    }

    TEST_CASE("levels are padded to the longest name, so messages line up") {
        DefaultFormatter formatter;
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 0};
        CHECK_EQ(formatLine(formatter, ts, "m", LogLevel::Error),
            "2026-10-23 10:24:05.000000000" + defaultSuffix("error", "m"));
        CHECK_EQ(formatLine(formatter, ts, "m", LogLevel::Warning),
            "2026-10-23 10:24:05.000000000" + defaultSuffix("warning", "m"));
        CHECK_EQ(formatLine(formatter, ts, "m", LogLevel::TraceL3),
            "2026-10-23 10:24:05.000000000" + defaultSuffix("tracel3", "m"));
    }

    TEST_CASE("the cached time text is refreshed when the second changes, either way") {
        DefaultFormatter formatter;
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
        DefaultFormatter formatter;
        ::timespec const ts{.tv_sec = k20261023_102405, .tv_nsec = 0};
        CHECK_EQ(formatLine(formatter, ts, ""), "2026-10-23 10:24:05.000000000" + defaultSuffix("info", ""));
        CHECK_EQ(formatLine(formatter, ts, "{} braces % and \t tab"),
            "2026-10-23 10:24:05.000000000" + defaultSuffix("info", "{} braces % and \t tab"));
    }

    TEST_CASE("lines are appended to the buffer") {
        DefaultFormatter formatter;
        fmt::memory_buffer out;
        formatter.format(out, kLocation, LogLevel::Info, kTimestamp, std::thread::id{}, "first");
        formatter.format(out, kLocation, LogLevel::Info, kTimestamp, std::thread::id{}, "second");
        auto const text = fmt::to_string(out);
        CHECK_EQ(std::count(text.begin(), text.end(), '\n'), 2);
        CHECK(text.find("first\n") != std::string::npos);
        CHECK(text.ends_with("second\n"));
    }

    TEST_CASE("baseName() keeps the file name of a path") {
        static_assert(FormatterBase::baseName("/a/b/c.cpp") == "c.cpp");
        static_assert(FormatterBase::baseName("c.cpp") == "c.cpp");
        static_assert(FormatterBase::baseName("dir/") == "");
        static_assert(FormatterBase::baseName("") == "");
    }
}

TEST_SUITE("Formatter fields") {

    TEST_CASE("time: std::tm in local time, any chrono spec") {
        CHECK_EQ(formatWith<"{time:%H:%M}">(), "10:24");
        CHECK_EQ(formatWith<"{time:%d.%m.%Y}">(), "23.10.2026");
        CHECK_EQ(formatWith<"{time:%FT%T}">(), "2026-10-23T10:24:05");
        ::tm tm{};
        ::gmtime_r(&k20261023_102405, &tm);
        CHECK_EQ(formatWith<"{time}">(), fmt::format("{}", tm)); // fmt's default for std::tm
    }

    TEST_CASE("utc: UTC, whatever the local time zone; several time fields") {
        TimeZone moscow{"Europe/Moscow"}; // UTC+3
        CHECK_EQ(formatWith<"{time:%H:%M} {utc:%H:%M} {time:%d}">(), "13:24 10:24 23");
    }

    TEST_CASE("ns, us, ms: the fraction of the second") {
        CHECK_EQ(formatWith<"{ns} {us} {ms}">(), "123456789 123456 123");
        CHECK_EQ(formatWith<"{ns:09} {us:06} {ms:03}">("", LogLevel::Info, {.tv_sec = 0, .tv_nsec = 5'001'000}),
            "005001000 005001 005");
    }

    TEST_CASE("level, thread, file, path, line, function, message") {
        CHECK_EQ(formatWith<"{level}">("", LogLevel::Warning), "warning");
        CHECK_EQ(formatWith<"{thread}">(), fmt::format("{}", std::this_thread::get_id()));
        CHECK_EQ(formatWith<"{file}:{line}">(), fmt::format("Formatter_test.cpp:{}", kLocation.line()));
        CHECK_EQ(formatWith<"{path}">(), kLocation.file_name());
        CHECK_EQ(formatWith<"{function}">(), kLocation.function_name());
        CHECK_EQ(formatWith<"{message}">("text with {} and %s"), "text with {} and %s");
    }

    TEST_CASE("thread is an integer: integer specs work") {
        auto const number = std::stoull(formatWith<"{thread}">());
        CHECK_EQ(formatWith<"{thread:x}">(), fmt::format("{:x}", number));
    }

    TEST_CASE("format specs are fmt's") {
        CHECK_EQ(formatWith<"[{level:>7}] [{level:<7}] [{level:^9}] [{level:*^9}]">(),
            "[   info] [info   ] [  info   ] [**info***]");
        CHECK_EQ(formatWith<"{ns:+} {ms:x} {line:>5}">(), fmt::format("+123456789 7b {:>5}", kLocation.line()));
        CHECK_EQ(formatWith<"{message:.3}">(), "hel");
        CHECK_EQ(formatWith<"{message:^9}">("привет"), fmt::format("{:^9}", "привет"));
    }

    TEST_CASE("literal text and doubled braces") {
        CHECK_EQ(formatWith<"no fields at all">(), "no fields at all");
        CHECK_EQ(formatWith<"{{{message}}}">(), "{hello}");
        CHECK_EQ(formatWith<"a}}b{{c">(), "a}b{c");
        CHECK_EQ(formatWith<"">(), "");
        CHECK_EQ(formatWith<"{message}{message}">(), "hellohello");
    }

    TEST_CASE("makeFormatter(): a FormatterBase for the sinks") {
        auto const formatter = makeFormatter<"{level}: {message}">({.colors = ColorMode::Never});
        REQUIRE(formatter);
        CHECK_EQ(formatLine(*formatter, kTimestamp, "x", LogLevel::Notice), "notice: x\n");
        CHECK_EQ(makeFormatter()->colorMode(), ColorMode::Auto); // the default pattern and options
    }
}

TEST_SUITE("Formatter colors") {

    TEST_CASE("default options: Auto, off until a sink resolves it") {
        DefaultFormatter formatter;
        CHECK_EQ(formatter.colorMode(), ColorMode::Auto);
        CHECK_FALSE(formatter.colorsEnabled());
    }

    TEST_CASE("Always: the whole line in the style of its level, reset before the newline") {
        constexpr std::array kLevels{LogLevel::Error, LogLevel::Warning, LogLevel::Notice, LogLevel::Info,
            LogLevel::Debug, LogLevel::TraceL1, LogLevel::TraceL2, LogLevel::TraceL3};
        for (auto const level : kLevels) {
            CAPTURE(logLevelToString(level));
            auto const& style = kDefaultLevelStyles[static_cast<std::size_t>(level)];
            CHECK_EQ(formatWith<"{level} {message}">("text", level, kTimestamp, ColorMode::Always),
                fmt::format(style, "{} text", logLevelToString(level)));
        }
    }

    TEST_CASE("the default pattern, colored") {
        DefaultFormatter formatter{{.colors = ColorMode::Always}};
        auto const line = formatLine(formatter, {.tv_sec = k20261023_102405, .tv_nsec = 0}, "m", LogLevel::Warning);
        auto plain = "2026-10-23 10:24:05.000000000" + defaultSuffix("warning", "m");
        plain.pop_back();
        CHECK_EQ(line, fmt::format(fmt::fg(fmt::terminal_color::yellow), "{}", plain) + "\n");
    }

    TEST_CASE("Never: no escape codes") {
        CHECK_EQ(formatWith<"{level}">("", LogLevel::Error, kTimestamp, ColorMode::Never), "error");
    }

    TEST_CASE("Auto: off until resolved, then as resolved; Never and Always ignore it") {
        Formatter<"x"> automatic{{.colors = ColorMode::Auto}};
        CHECK_EQ(formatLine(automatic, {}, "", LogLevel::Error), "x\n");
        automatic.resolveAutoColors(true);
        CHECK(automatic.colorsEnabled());
        CHECK_EQ(formatLine(automatic, {}, "", LogLevel::Error), fmt::format(kDefaultLevelStyles[0], "x") + "\n");
        automatic.resolveAutoColors(false);
        CHECK_EQ(formatLine(automatic, {}, "", LogLevel::Error), "x\n");

        DefaultFormatter never{{.colors = ColorMode::Never}};
        never.resolveAutoColors(true);
        CHECK_FALSE(never.colorsEnabled());

        DefaultFormatter always{{.colors = ColorMode::Always}};
        always.resolveAutoColors(false);
        CHECK(always.colorsEnabled());
    }

    TEST_CASE("custom level styles; an empty style writes no escape codes at all") {
        auto styles = kDefaultLevelStyles;
        styles[static_cast<std::size_t>(LogLevel::Info)] = fmt::fg(fmt::color::orange) | fmt::emphasis::italic;
        styles[static_cast<std::size_t>(LogLevel::Debug)] = fmt::text_style{};
        Formatter<"{level}"> formatter{{.colors = ColorMode::Always, .levelStyles = styles}};
        CHECK_EQ(formatLine(formatter, {}, "", LogLevel::Info),
            fmt::format(fmt::fg(fmt::color::orange) | fmt::emphasis::italic, "info") + "\n");
        CHECK_EQ(formatLine(formatter, {}, "", LogLevel::Debug), "debug\n");
    }
}

} // namespace turboq::logger::testing
