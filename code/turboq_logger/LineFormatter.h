// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <ctime>
#include <source_location>
#include <string_view>
#include <thread>

#include <fmt/format.h>

#include "LogLevel.h"

namespace turboq::logger {

/// Formats a log message as one text line, in local time:
///
///     2026-10-23 13:24:05.123456789 info    [140367476504512] example.cpp:28 hello world
///
/// The date and time down to the second are cached: consecutive messages of the same second skip
/// localtime_r(). Not thread-safe: one formatter per sink (sinks run on the backend thread).
class LineFormatter {
private:
    static constexpr std::size_t kTimeSize = sizeof("YYYY-MM-DD HH:MM:SS") - 1;

    std::time_t cachedSecond_{-1};
    std::array<char, kTimeSize> cachedTime_{};

public:
    /// Append the line for this message to out, '\n' included
    void format(fmt::memory_buffer& out, std::source_location const& location, LogLevel level,
        ::timespec const& timestamp, std::thread::id const& threadID, std::string_view message);

    /// The file name part of a path ("a/b/c.cpp" -> "c.cpp")
    [[nodiscard]] static constexpr auto baseName(std::string_view path) noexcept -> std::string_view {
        auto const slash = path.rfind('/');
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }
};

} // namespace turboq::logger
