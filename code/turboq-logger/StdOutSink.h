// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include <fmt/color.h>
#include <fmt/format.h>

#include "Formatter.h"
#include "Sink.h"

namespace turboq::logger {

/// Whether the sink colors lines: each line whole, in the style of its level
enum class ColorMode {
    Never,  ///< no escape codes
    Always, ///< ANSI escape codes
    Auto,   ///< the sink decides: colors when stdout is a terminal and NO_COLOR isn't set
};

/// Default style of a line, per LogLevel
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

struct StdOutSinkOptions {
    /// Whether lines are colored; ColorMode::Auto resolves at the sink's construction
    ColorMode colors = ColorMode::Auto;
    /// Style of a line, indexed by its LogLevel; a default-constructed style colors nothing
    std::array<fmt::text_style, kLogLevelCount> levelStyles = kDefaultLevelStyles;
};

/// Writes messages to stdout, one line each (see FormatterBase, Formatter).
///
///     BackendThread::setSink(std::make_shared<StdOutSink>()); // colored on a terminal
///
/// Colors (StdOutSinkOptions::colors): when enabled, the whole line is written in the style of its
/// level (StdOutSinkOptions::levelStyles), reset before the '\n'. ColorMode::Auto colors when
/// stdout is a terminal and the NO_COLOR environment variable is unset or empty
/// (https://no-color.org).
///
/// Goes through stdio: line-buffered when stdout is a terminal, fully buffered when it is a pipe
/// or a file -- flush() (BackendThread::flush(), stop()) pushes the buffer out.
class StdOutSink final : public Sink {
private:
    ColorMode colorMode_;
    bool colorsEnabled_ = false;
    /// escape code starting each level's style ("" for an empty style)
    std::array<std::string, kLogLevelCount> colorCodes_;

    std::unique_ptr<FormatterBase> formatter_;
    fmt::memory_buffer line_;

public:
    /// nullptr: DefaultFormatter. ColorMode::Auto resolves here: colors iff stdout is a terminal
    /// and NO_COLOR isn't set to a non-empty value.
    explicit StdOutSink(std::unique_ptr<FormatterBase> formatter = nullptr, StdOutSinkOptions const& options = {});

    [[nodiscard]] auto formatter() const noexcept -> FormatterBase const& {
        return *formatter_;
    }

    /// True if lines are colored
    [[nodiscard]] auto colorsEnabled() const noexcept -> bool {
        return colorsEnabled_;
    }

    /// \see Sink
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override;

    /// \see Sink
    void flush() override;
};

} // namespace turboq::logger
