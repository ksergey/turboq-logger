// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include "Common.h"
#include "Sink.h"

namespace turboq::logger {

/// Time zone a file sink uses for the timestamps it writes and for any time-based file switching
enum class TimeZone { Local, UTC };

/// Base of sinks writing to files: formats each message as a line and writes it to the current
/// file. A derived sink decides when to open which file: beforeWrite() is called before every
/// message, and openFile() switches to a file.
///
/// Every file a file sink opens starts with all LogLevel::Always messages received so far, so each
/// file is self-contained (e.g. a startup banner or the configuration logged once at Always shows
/// up in every daily file). They are kept in memory for that: Always is meant for a few messages.
///
/// Line format:
///   2026-10-01 12:34:56.123456789 NOTICE [140245] Main.cpp:42 message
class BasicFileSink : public Sink {
private:
    TimeZone const timeZone_;
    std::FILE* file_{nullptr};
    std::filesystem::path path_;

    // Lines of the LogLevel::Always messages received so far, replayed into every file opened
    std::vector<std::string> alwaysLines_;

    // Reused for every line; plus the formatted date and time of the last second seen, as
    // converting seconds to a date is the costly part and consecutive messages share it
    fmt::memory_buffer line_;
    std::time_t cachedSecond_{-1};
    fmt::memory_buffer cachedDateTime_;

public:
    BasicFileSink(BasicFileSink const&) = delete;
    BasicFileSink& operator=(BasicFileSink const&) = delete;

    ~BasicFileSink() override;

    /// Write the message as a line to the current file (after beforeWrite() had the chance to
    /// switch files). Throws std::system_error if no file could be opened or writing fails.
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) final;

    /// Push the current file's buffered lines to the OS and the OS's to the disk (fflush() and
    /// fdatasync()). Throws std::system_error on failure.
    void flush() override;

    /// The file written to, empty if none is open yet
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& {
        return path_;
    }

    [[nodiscard]] auto timeZone() const noexcept -> TimeZone {
        return timeZone_;
    }

protected:
    explicit BasicFileSink(TimeZone timeZone) noexcept;

    /// Called before every message is written, with its timestamp: open the file it belongs in
    /// with openFile() if it isn't the current one. Must leave a file open.
    virtual void beforeWrite(::timespec const& timestamp) = 0;

    /// Close the current file (if any) and open path for appending, creating missing parent
    /// directories, then write all LogLevel::Always messages received so far to it.
    /// Throws std::system_error on failure.
    void openFile(std::filesystem::path const& path);

    [[nodiscard]] auto isOpen() const noexcept -> bool {
        return file_ != nullptr;
    }

    /// Break seconds since the epoch down in the sink's time zone
    [[nodiscard]] auto toTm(std::time_t seconds) const -> std::tm;

    /// Seconds since the epoch of a broken-down time in the sink's time zone (fields may be out of
    /// range, e.g. tm_mday = 32, and are normalized)
    [[nodiscard]] auto fromTm(std::tm tm) const -> std::time_t;

private:
    void writeLine(std::string_view line);
    void closeFile() noexcept;
};

} // namespace turboq::logger
