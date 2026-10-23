// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <filesystem>

#include <fmt/format.h>

#include "LineFormatter.h"
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

/// Writes messages to one file, one line each (see LineFormatter).
///
///     BackendThread::setSink(std::make_shared<FileSink>("logs/app.log"));
///
/// The file is opened on construction (so a bad path fails right away), for appending unless
/// FileSinkOptions::truncate. Missing parent directories are created. For a new file every day,
/// see DailyFileSink.
class FileSink final : public Sink {
private:
    detail::LogFile file_;
    bool fsyncOnFlush_;
    LineFormatter formatter_;
    fmt::memory_buffer line_;

public:
    /// Open the file. Throws std::system_error (or std::filesystem::filesystem_error) if it can't
    /// be opened.
    explicit FileSink(std::filesystem::path path, FileSinkOptions const& options = {});

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
};

} // namespace turboq::logger
