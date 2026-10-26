// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <fmt/color.h>
#include <fmt/format.h>

#include "Formatter.h"
#include "Sink.h"

namespace turboq::logger {

/// Writes messages to stdout, one line each (see FormatterBase, Formatter).
///
///     BackendThread::setSink(std::make_shared<StdOutSink>()); // the level colored on a terminal
///
/// ColorMode::Auto colors when stdout is a terminal and the NO_COLOR environment variable is unset
/// or empty (https://no-color.org).
///
/// Goes through stdio: line-buffered when stdout is a terminal, fully buffered when it is a pipe
/// or a file -- flush() (BackendThread::flush(), stop()) pushes the buffer out.
class StdOutSink final : public Sink {
private:
    std::unique_ptr<FormatterBase> formatter_;
    fmt::memory_buffer line_;

public:
    /// nullptr: DefaultFormatter
    explicit StdOutSink(std::unique_ptr<FormatterBase> formatter = nullptr);

    [[nodiscard]] auto formatter() const noexcept -> FormatterBase const& {
        return *formatter_;
    }

    /// \see Sink
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override;

    /// \see Sink
    void flush() override;
};

} // namespace turboq::logger
