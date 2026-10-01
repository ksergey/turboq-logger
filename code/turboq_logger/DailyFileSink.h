// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <ctime>
#include <limits>
#include <string>

#include "FileSink.h"

namespace turboq::logger {

/// File sink starting a new file every day.
///
/// The file name comes from a pattern formatted with the date, in fmt/strftime syntax, e.g.
/// "logs/app-{:%Y-%m-%d}.log" gives logs/app-2026-10-01.log. Days follow the messages' timestamps
/// (in the sink's time zone, local by default): the first message timestamped on a new day opens
/// that day's file. A late message from the previous day -- possible when entries of different
/// threads aren't ordered -- goes to the current file instead of reopening the old one.
///
/// Files are opened for appending, so a restarted process carries on today's file. As with every
/// file sink, each file starts with all LogLevel::Always messages received so far.
class DailyFileSink final : public BasicFileSink {
private:
    std::string const pattern_;
    // When the current file's day ends: a message at or after it opens the next file
    std::time_t nextDayStart_{std::numeric_limits<std::time_t>::min()};

public:
    /// Throws std::invalid_argument if pattern isn't a valid fmt pattern for a date
    explicit DailyFileSink(std::string pattern, TimeZone timeZone = TimeZone::Local);

private:
    void beforeWrite(::timespec const& timestamp) override;
};

} // namespace turboq::logger
