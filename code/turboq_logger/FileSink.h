// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <fmt/format.h>

#include "Formatter.h"
#include "Sink.h"
#include "detail/LogFile.h"

namespace turboq::logger {

struct FileSinkOptions {
    /// stdio buffer of the file
    std::size_t bufferSize = 64 * 1024;
    /// flush() also calls fsync(): the messages survive a machine crash, not only a process one.
    /// Costly: a flush then waits for the disk.
    bool fsyncOnFlush = false;
    /// Start with an empty file instead of appending to an existing one
    bool truncate = false;
};

/// Writes messages to one file, one line each (see FormatterBase, Formatter).
///
///     BackendThread::setSink(std::make_shared<FileSink>("logs/app.log"));
///
/// The file is opened on construction (so a bad path fails right away), for appending unless
/// FileSinkOptions::truncate. Missing parent directories are created. ColorMode::Auto means no
/// colors. For a new file every day, see DailyFileSink.
class FileSink final : public Sink {
private:
    detail::LogFile file_;
    bool fsyncOnFlush_;
    std::unique_ptr<FormatterBase> formatter_;
    fmt::memory_buffer line_;

public:
    /// Open the file. formatter nullptr: DefaultFormatter. Throws std::system_error (or
    /// std::filesystem::filesystem_error) if the file can't be opened.
    explicit FileSink(std::filesystem::path path, std::unique_ptr<FormatterBase> formatter = nullptr,
        FileSinkOptions const& options = {});

    FileSink(FileSink const&) = delete;
    FileSink& operator=(FileSink const&) = delete;

    /// Throws std::system_error if the write fails
    void write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
        std::thread::id const& threadID, std::string_view message) override;

    /// fflush(), and fsync() if FileSinkOptions::fsyncOnFlush
    void flush() override;

    /// The file being written
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& {
        return file_.path();
    }

    [[nodiscard]] auto formatter() const noexcept -> FormatterBase const& {
        return *formatter_;
    }
};

} // namespace turboq::logger
