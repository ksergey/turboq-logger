// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <ctime>
#include <source_location>
#include <string_view>
#include <thread>

#include "Common.h"

namespace turboq::logger {

/// Destination of formatted log messages: a file, the console, a network socket...
///
/// The backend calls a sink only from its own thread, so an implementation needs no locking for
/// that. write() may buffer; flush() must push out everything written so far.
class Sink {
public:
    virtual ~Sink() = default;

    /// Accept one log message. The arguments are only valid during the call: copy what you keep.
    ///
    /// @param[in] location   where the message was logged
    /// @param[in] level      its verbosity level
    /// @param[in] timestamp  when it was logged (wall-clock time)
    /// @param[in] threadID   the thread that logged it
    /// @param[in] message    the formatted text
    virtual void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) = 0;

    /// Push everything accepted so far out to the medium (e.g. fflush()/fsync() for a file), so it
    /// survives the process. Called when a flush is requested (see BackendBase::flush()).
    virtual void flush() = 0;
};

/// Sink discarding everything
class NullSink final : public Sink {
public:
    void write(std::source_location const&, LogLevel, ::timespec const&, std::thread::id const&,
        std::string_view) override {}

    void flush() override {}
};

} // namespace turboq::logger
