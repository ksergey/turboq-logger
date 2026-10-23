// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <fmt/format.h>

#include "LineFormatter.h"
#include "Sink.h"

namespace turboq::logger {

/// Writes messages to stdout, one line each (see LineFormatter).
///
/// Goes through stdio: line-buffered when stdout is a terminal, fully buffered when it is a pipe
/// or a file -- flush() (BackendThread::flush(), stop()) pushes the buffer out.
class StdOutSink final : public Sink {
private:
    LineFormatter formatter_;
    fmt::memory_buffer line_;

public:
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override;

    void flush() override;
};

} // namespace turboq::logger
