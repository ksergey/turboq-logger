// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "LineFormatter.h"

#include <iterator>

#include <fmt/std.h>

namespace turboq::logger {

void LineFormatter::format(fmt::memory_buffer& out, std::source_location const& location, LogLevel level,
    ::timespec const& timestamp, std::thread::id const& threadID, std::string_view message) {
    if (timestamp.tv_sec != cachedSecond_) [[unlikely]] {
        ::tm local{};
        ::localtime_r(&timestamp.tv_sec, &local);
        fmt::format_to_n(cachedTime_.data(), cachedTime_.size(), "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
            local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
        cachedSecond_ = timestamp.tv_sec;
    }

    auto it = std::back_inserter(out);
    out.append(cachedTime_.data(), cachedTime_.data() + cachedTime_.size());
    fmt::format_to(it, ".{:09} {:<7} [{}] {}:{} ", timestamp.tv_nsec, logLevelToString(level), threadID,
        baseName(location.file_name()), location.line());
    out.append(message.data(), message.data() + message.size());
    out.push_back('\n');
}

} // namespace turboq::logger
