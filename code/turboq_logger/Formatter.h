// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/compile.h>
#include <fmt/format.h>

#include "LogLevel.h"

namespace turboq::logger {

/// Whether a formatter colors its lines (each line whole, in the style of its level)
enum class ColorMode {
    Never,  ///< no escape codes
    Always, ///< ANSI escape codes
    Auto,   ///< the sink decides: StdOutSink colors when stdout is a terminal and NO_COLOR isn't set,
            ///< the file sinks never do
};

/// Number of LogLevel values (index of FormatterOptions::levelStyles)
inline constexpr std::size_t kLogLevelCount = 8;

/// Default line style, per LogLevel
inline constexpr std::array<fmt::text_style, kLogLevelCount> kDefaultLevelStyles = {
    fmt::fg(fmt::terminal_color::red) | fmt::emphasis::bold, // Error
    fmt::fg(fmt::terminal_color::yellow),                    // Warning
    fmt::fg(fmt::terminal_color::cyan),                      // Notice
    fmt::fg(fmt::terminal_color::green),                     // Info
    fmt::fg(fmt::terminal_color::blue),                      // Debug
    fmt::emphasis::faint,                                    // TraceL1
    fmt::emphasis::faint,                                    // TraceL2
    fmt::emphasis::faint,                                    // TraceL3
};

struct FormatterOptions {
    ColorMode colors = ColorMode::Auto;
    /// Style of a line, indexed by its LogLevel
    std::array<fmt::text_style, kLogLevelCount> levelStyles = kDefaultLevelStyles;
};

/// The values of one log line, as the fields of a pattern see them
struct LineFields {
    ::timespec timestamp;
    std::string_view level;
    std::uint64_t thread;
    std::string_view file;
    std::string_view path;
    std::uint_least32_t line;
    std::string_view function;
    std::string_view message;
};

/// Formats a log message as one text line; what the text sinks take. Colors the whole line (see
/// ColorMode) around the text of formatLine(). Not thread-safe: one formatter per sink (sinks run
/// on the backend thread).
class FormatterBase {
private:
    ColorMode colorMode_;
    bool colorsEnabled_;
    // escape code starting each level's style ("" for an empty style)
    std::array<std::string, kLogLevelCount> colorCodes_;

public:
    explicit FormatterBase(FormatterOptions const& options = {});
    virtual ~FormatterBase() = default;

    FormatterBase(FormatterBase const&) = delete;
    FormatterBase& operator=(FormatterBase const&) = delete;

    /// Append the line for this message to out, '\n' included
    void format(fmt::memory_buffer& out, std::source_location const& location, LogLevel level,
        ::timespec const& timestamp, std::thread::id const& threadID, std::string_view message);

    /// Settle ColorMode::Auto: colors on or off. Called by the sinks, which know whether they write
    /// to a terminal; no effect with ColorMode::Never or ColorMode::Always.
    void resolveAutoColors(bool enabled) noexcept;

    [[nodiscard]] auto colorMode() const noexcept -> ColorMode {
        return colorMode_;
    }

    /// True if lines are colored
    [[nodiscard]] auto colorsEnabled() const noexcept -> bool {
        return colorsEnabled_;
    }

    /// The file name part of a path ("a/b/c.cpp" -> "c.cpp")
    [[nodiscard]] static constexpr auto baseName(std::string_view path) noexcept -> std::string_view {
        auto const slash = path.rfind('/');
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

protected:
    /// Append the text of the line (no color, no '\n')
    virtual void formatLine(fmt::memory_buffer& out, LineFields const& fields) = 0;
};

/// A string literal as a template argument: Formatter<"{level} {message}">
template <std::size_t N>
struct FixedString {
    char data[N]{};

    constexpr FixedString(char const (&text)[N]) noexcept {
        std::copy_n(text, N, data);
    }

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
        return {data, N - 1};
    }
};

namespace detail {

// Declared, never defined, not constexpr: reaching one while translating a pattern (a constant
// expression) is a compile error whose message names the problem.
[[noreturn]] void unknown_field_in_log_line_pattern();
[[noreturn]] void unmatched_brace_in_log_line_pattern();
[[noreturn]] void nested_field_in_log_line_pattern();
[[noreturn]] void too_many_time_fields_in_log_line_pattern();

/// Positional argument of each non-time field in a translated pattern; the time fields follow
enum FieldArg : std::size_t {
    kArgNs,
    kArgUs,
    kArgMs,
    kArgLevel,
    kArgThread,
    kArgFile,
    kArgPath,
    kArgLine,
    kArgFunction,
    kArgMessage,
    kArgFirstTime, ///< {time} / {utc} fields, in order: their text, cached once per second
};

inline constexpr std::size_t kMaxTimeFields = 8;

struct TimeField {
    bool utc = false;
    std::string_view spec; ///< chrono spec, e.g. "%F %T" (may be empty)
};

/// A pattern with its names replaced by positional arguments:
///   "{time:%T}.{ns:09} {level:<7} {message}"  ->  "{10}.{0:09} {3:<7} {9}"
template <std::size_t N>
struct PatternTranslation {
    std::array<char, N> text{};
    std::size_t size = 0;
    std::array<TimeField, kMaxTimeFields> times{};
    std::size_t timeCount = 0;
    bool hasLocalTime = false;
    bool hasUtcTime = false;

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
        return {text.data(), size};
    }
};

// Index-based string helpers for the translation. std::string_view::find() compares a pointer
// into the pattern (a template parameter object) with null, which GCC doesn't accept in a
// constant expression under -fsanitize=undefined.

constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

constexpr auto findChar(std::string_view text, char c, std::size_t from = 0) noexcept -> std::size_t {
    for (std::size_t i = from; i < text.size(); ++i) {
        if (text[i] == c) {
            return i;
        }
    }
    return kNotFound;
}

constexpr auto equals(std::string_view lhs, std::string_view rhs) noexcept -> bool {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) {
            return false;
        }
    }
    return true;
}

/// Translate a pattern; N > pattern.size() (a field never grows: names are longer than indices)
template <std::size_t N>
consteval auto translatePattern(std::string_view pattern) -> PatternTranslation<N> {
    constexpr std::pair<std::string_view, std::size_t> kFields[] = {
        {"ns", kArgNs},
        {"us", kArgUs},
        {"ms", kArgMs},
        {"level", kArgLevel},
        {"thread", kArgThread},
        {"file", kArgFile},
        {"path", kArgPath},
        {"line", kArgLine},
        {"function", kArgFunction},
        {"message", kArgMessage},
    };

    PatternTranslation<N> result;
    auto const put = [&](std::string_view text) {
        for (char const c : text) {
            result.text[result.size++] = c;
        }
    };
    auto const putIndex = [&](std::size_t index) {
        if (index >= 10) {
            result.text[result.size++] = static_cast<char>('0' + index / 10);
        }
        result.text[result.size++] = static_cast<char>('0' + index % 10);
    };

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        char const c = pattern[i];
        if (c == '}') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
                put("}}");
                ++i;
                continue;
            }
            unmatched_brace_in_log_line_pattern();
        }
        if (c != '{') {
            result.text[result.size++] = c;
            continue;
        }
        if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
            put("{{");
            ++i;
            continue;
        }

        auto const close = findChar(pattern, '}', i + 1);
        if (close == kNotFound) {
            unmatched_brace_in_log_line_pattern();
        }
        auto const field = pattern.substr(i + 1, close - i - 1);
        if (findChar(field, '{') != kNotFound) {
            nested_field_in_log_line_pattern();
        }
        auto const colon = findChar(field, ':');
        auto const name = colon == kNotFound ? field : field.substr(0, colon);
        auto const spec = colon == kNotFound ? std::string_view{} : field.substr(colon + 1);

        put("{");
        if (equals(name, "time") || equals(name, "utc")) {
            if (result.timeCount == kMaxTimeFields) {
                too_many_time_fields_in_log_line_pattern();
            }
            auto const utc = equals(name, "utc");
            result.hasLocalTime = result.hasLocalTime || !utc;
            result.hasUtcTime = result.hasUtcTime || utc;
            result.times[result.timeCount] = TimeField{.utc = utc, .spec = spec};
            putIndex(kArgFirstTime + result.timeCount++); // the spec applies to the cached text
        } else {
            auto index = kNotFound;
            for (auto const& [fieldName, fieldIndex] : kFields) {
                if (equals(name, fieldName)) {
                    index = fieldIndex;
                }
            }
            if (index == kNotFound) {
                unknown_field_in_log_line_pattern();
            }
            putIndex(index);
            if (colon != kNotFound) {
                put(":");
                put(spec);
            }
        }
        put("}");
        i = close;
    }
    return result;
}

template <FixedString Pattern>
struct PatternInfo {
    static constexpr std::string_view source = Pattern.view();
    static constexpr auto translation = translatePattern<source.size() + 1>(source);
};

/// The translated pattern, as a format string fmt compiles (see FMT_COMPILE)
template <FixedString Pattern>
struct CompiledLinePattern : fmt::compiled_string {
    using char_type = char;

    constexpr explicit operator fmt::string_view() const noexcept {
        auto const view = PatternInfo<Pattern>::translation.view();
        return {view.data(), view.size()};
    }
};

/// "{:<spec>}" of the I-th time field, as a format string fmt compiles
template <FixedString Pattern, std::size_t I>
struct CompiledTimePattern : fmt::compiled_string {
    using char_type = char;

    static constexpr auto text = [] {
        constexpr auto spec = PatternInfo<Pattern>::translation.times[I].spec;
        std::array<char, spec.size() + 4> result{};
        std::size_t size = 0;
        result[size++] = '{';
        if (!spec.empty()) {
            result[size++] = ':';
            for (char const c : spec) {
                result[size++] = c;
            }
        }
        result[size++] = '}';
        return std::pair{result, size};
    }();

    constexpr explicit operator fmt::string_view() const noexcept {
        return {text.first.data(), text.second};
    }
};

} // namespace detail

/// A FormatterBase for a pattern known at compile time: an fmt format string with named fields
/// ('\n' is appended):
///
///   {time}      std::tm, local time   any fmt chrono spec: {time:%F %T}, {time:%H:%M}
///   {utc}       std::tm, UTC          same
///   {ns} {us} {ms}  fraction of the second, integer: {ns:09}, {us:06}, {ms:03}
///   {level}     "info", "warning"...  {level:<7}, {level:>7}
///   {thread}    thread ID, integer    {thread:x}
///   {file}      file name of the call site ("main.cpp"); {path} the full path
///   {line}      line of the call site
///   {function}  function of the call site
///   {message}   the formatted message
///
/// Literal braces are doubled: "{{" and "}}".
///
///     BackendThread::setSink(std::make_shared<StdOutSink>(
///         makeFormatter<"{time:%T}.{ms:03} {level:>7} {message}">()));
///
/// The pattern is checked and compiled at compile time: an unknown field, a bad format spec or an
/// unbalanced brace doesn't compile. The names become positional arguments of an fmt compiled
/// format; the text of the time fields only depends on the second and is formatted once per second.
template <FixedString Pattern>
class Formatter final : public FormatterBase {
private:
    static constexpr auto const& kTranslation = detail::PatternInfo<Pattern>::translation;
    static constexpr std::size_t kTimeCount = kTranslation.timeCount;

    std::time_t cachedSecond_{std::numeric_limits<std::time_t>::min()};
    std::array<std::string, kTimeCount> timeTexts_;

public:
    explicit Formatter(FormatterOptions const& options = {}) : FormatterBase{options} {}

    /// The pattern, as written
    [[nodiscard]] static constexpr auto pattern() noexcept -> std::string_view {
        return Pattern.view();
    }

protected:
    void formatLine(fmt::memory_buffer& out, LineFields const& fields) override {
        if constexpr (kTimeCount > 0) {
            if (fields.timestamp.tv_sec != cachedSecond_) [[unlikely]] {
                this->updateTime(fields.timestamp.tv_sec);
            }
        }
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            fmt::format_to(std::back_inserter(out), detail::CompiledLinePattern<Pattern>{}, fields.timestamp.tv_nsec,
                fields.timestamp.tv_nsec / 1000, fields.timestamp.tv_nsec / 1000000, fields.level, fields.thread,
                fields.file, fields.path, fields.line, fields.function, fields.message,
                std::string_view{timeTexts_[I]}...);
        }(std::make_index_sequence<kTimeCount>{});
    }

private:
    void updateTime(std::time_t second) {
        ::tm local{};
        ::tm utc{};
        if constexpr (kTranslation.hasLocalTime) {
            ::localtime_r(&second, &local);
        }
        if constexpr (kTranslation.hasUtcTime) {
            ::gmtime_r(&second, &utc);
        }
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((timeTexts_[I].clear(),
                 fmt::format_to(std::back_inserter(timeTexts_[I]), detail::CompiledTimePattern<Pattern, I>{},
                     kTranslation.times[I].utc ? utc : local)),
                ...);
        }(std::make_index_sequence<kTimeCount>{});
        cachedSecond_ = second;
    }
};

/// The default pattern: "2026-10-23 13:24:05.123456789 info    [140367476504512] main.cpp:42 text"
inline constexpr FixedString kDefaultPattern{
    "{time:%Y-%m-%d %H:%M:%S}.{ns:09} {level:<7} [{thread}] {file}:{line} {message}"};

using DefaultFormatter = Formatter<kDefaultPattern>;

/// A Formatter for Pattern, as the sinks take it
template <FixedString Pattern = kDefaultPattern>
[[nodiscard]] auto makeFormatter(FormatterOptions const& options = {}) -> std::unique_ptr<FormatterBase> {
    return std::make_unique<Formatter<Pattern>>(options);
}

} // namespace turboq::logger
