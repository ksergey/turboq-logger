// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "DailyFileSink.h"

#include <utility>

namespace turboq::logger {
namespace {

/// Local midnight ending the day of time (mktime() accounts for DST: a day may be 23 or 25 hours)
auto nextMidnight(std::time_t time) -> std::time_t {
    ::tm local{};
    ::localtime_r(&time, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_mday += 1; // normalized by mktime(): month and year roll over
    local.tm_isdst = -1;
    return ::mktime(&local);
}

auto now() -> std::time_t {
    ::timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

} // namespace

DailyFileSink::DailyFileSink(
    std::filesystem::path prefix, std::unique_ptr<FormatterBase> formatter, DailyFileSinkOptions const& options)
    : prefix_{std::move(prefix)}, options_{options},
      formatter_{formatter ? std::move(formatter) : std::make_unique<DefaultFormatter>()} {
    this->open(now());
}

DailyFileSink::~DailyFileSink() = default; // fclose() flushes

auto DailyFileSink::filePath(std::filesystem::path const& prefix, std::time_t time) -> std::filesystem::path {
    ::tm local{};
    ::localtime_r(&time, &local);
    auto path = prefix;
    path += fmt::format("_{:04}{:02}{:02}.log", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return path;
}

void DailyFileSink::open(std::time_t time) {
    // the new file is opened first: only then is the previous one closed (which flushes it)
    file_ = detail::LogFile{filePath(prefix_, time), detail::LogFile::Mode::Append, options_.bufferSize};
    nextDayStart_ = nextMidnight(time);
}

void DailyFileSink::write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
    std::thread::id const& threadID, std::string_view message) {
    if (timestamp.tv_sec >= nextDayStart_) [[unlikely]] {
        this->open(timestamp.tv_sec);
    }

    line_.clear();
    formatter_->format(line_, location, level, timestamp, threadID, message);
    file_.write(std::string_view{line_.data(), line_.size()});
}

void DailyFileSink::flush() {
    file_.flush(options_.fsyncOnFlush);
}

} // namespace turboq::logger
