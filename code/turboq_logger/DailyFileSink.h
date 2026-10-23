// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <memory>

#include <fmt/format.h>

#include "Formatter.h"
#include "Sink.h"
#include "detail/LogFile.h"

namespace turboq::logger {

struct DailyFileSinkOptions {
    /// stdio buffer of the file
    std::size_t bufferSize = 64 * 1024;
    /// flush() also calls fsync(): the messages survive a machine crash, not only a process one.
    /// Costly: a flush then waits for the disk.
    bool fsyncOnFlush = false;
};

/// Writes messages to the file "<prefix>_YYYYMMDD.log" of the day they were logged on, one line
/// each (see Formatter); a new file starts at local midnight.
///
///     BackendThread::setSink(std::make_shared<DailyFileSink>("logs/app")); // logs/app_20261023.log
///
/// - The day comes from the message timestamp, in local time. The file only moves forward: a
///   message timestamped before the current day (threads' messages arrive slightly out of order)
///   goes to the current file rather than reopening yesterday's.
/// - The file of the current day is opened on construction (so a bad path fails right away), the
///   next one by the first message of the next day: a day without messages has no file.
/// - Files are opened for appending: a restart the same day continues today's file. Missing
///   parent directories are created.
/// - ColorMode::Auto means no colors.
class DailyFileSink final : public Sink {
private:
    std::filesystem::path prefix_;
    DailyFileSinkOptions options_;
    std::unique_ptr<FormatterBase> formatter_;
    fmt::memory_buffer line_;

    detail::LogFile file_;
    // local midnight ending the day of the open file
    std::time_t nextDayStart_{0};

public:
    /// Open today's file. formatter nullptr: DefaultFormatter. Throws std::system_error (or
    /// std::filesystem::filesystem_error) if the file can't be opened.
    explicit DailyFileSink(std::filesystem::path prefix, std::unique_ptr<FormatterBase> formatter = nullptr,
        DailyFileSinkOptions const& options = {});

    ~DailyFileSink() override;

    DailyFileSink(DailyFileSink const&) = delete;
    DailyFileSink& operator=(DailyFileSink const&) = delete;

    /// Throws std::system_error if the next day's file can't be opened (the message is then
    /// dropped and the next one retries) or the write fails
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override;

    /// fflush(), and fsync() if DailyFileSinkOptions::fsyncOnFlush
    void flush() override;

    /// The file being written
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& {
        return file_.path();
    }

    [[nodiscard]] auto formatter() const noexcept -> FormatterBase const& {
        return *formatter_;
    }

    /// The file for the day containing time: "<prefix>_YYYYMMDD.log" (local time)
    [[nodiscard]] static auto filePath(std::filesystem::path const& prefix, std::time_t time) -> std::filesystem::path;

private:
    void open(std::time_t time);
};

} // namespace turboq::logger
