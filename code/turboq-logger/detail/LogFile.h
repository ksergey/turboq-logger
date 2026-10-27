// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

namespace turboq::logger::detail {

/// A log file written through a stdio buffer; the common part of the file sinks
class LogFile {
private:
    struct FileCloser {
        void operator()(std::FILE* file) const noexcept;
    };

    std::unique_ptr<std::FILE, FileCloser> file_;
    std::filesystem::path path_;

public:
    enum class Mode {
        Append,   ///< keep the existing content
        Truncate, ///< start empty
    };

    LogFile() = default;

    /// Open (create) the file, creating missing parent directories. Throws std::system_error (or
    /// std::filesystem::filesystem_error) on failure.
    LogFile(std::filesystem::path path, Mode mode, std::size_t bufferSize);

    /// Closing flushes the buffer
    ~LogFile() = default;

    LogFile(LogFile&&) noexcept = default;
    LogFile& operator=(LogFile&&) noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(file_);
    }

    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& {
        return path_;
    }

    /// Append data to the buffer (written out when it fills up). Throws std::system_error. On a
    /// failed write the stream is cleared for the next write() to retry.
    void write(std::string_view data);

    /// Write the buffer out (fflush()), and fsync() if sync. Throws std::system_error (a failed
    /// fflush() leaves the stream cleared for the next flush() to retry).
    void flush(bool sync);
};

} // namespace turboq::logger::detail
