// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "DailyFileSink.h"

#include <stdexcept>
#include <utility>

#include <fmt/chrono.h>

namespace turboq::logger {

DailyFileSink::DailyFileSink(std::string pattern, TimeZone timeZone)
    : BasicFileSink{timeZone}, pattern_{std::move(pattern)} {
    // fail here rather than on the first message, deep inside the backend. With a real date:
    // std::tm{} has tm_mday = 0, which fmt rejects (asserts in debug builds)
    try {
        (void)fmt::format(fmt::runtime(pattern_), toTm(0));
    } catch (fmt::format_error const& e) {
        // a standard exception: callers needn't know about fmt (and fmt::format_error's vtable,
        // emitted in our -fno-rtti code, lacks type info for RTTI-based tools)
        throw std::invalid_argument{fmt::format("invalid log file name pattern '{}': {}", pattern_, e.what())};
    }
}

void DailyFileSink::beforeWrite(::timespec const& timestamp) {
    if (isOpen() && timestamp.tv_sec < nextDayStart_) {
        return; // today's file, or a late message from an earlier day
    }

    auto tm = toTm(timestamp.tv_sec);
    openFile(fmt::format(fmt::runtime(pattern_), tm));

    // midnight starting the next day (fromTm() normalizes tm_mday overflowing the month)
    tm.tm_mday += 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    nextDayStart_ = fromTm(tm);
}

} // namespace turboq::logger
