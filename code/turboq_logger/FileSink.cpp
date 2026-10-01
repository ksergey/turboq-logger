// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "FileSink.h"

#include <cerrno>
#include <iterator>
#include <system_error>

#include <unistd.h>

#include <fmt/chrono.h>
#include <fmt/std.h>

namespace turboq::logger {
namespace {

[[noreturn]] void throwErrno(char const* what) {
    throw std::system_error{errno, std::generic_category(), what};
}

/// File name without directories, for a shorter line
[[nodiscard]] auto baseName(char const* path) noexcept -> std::string_view {
    auto const view = std::string_view{path};
    auto const slash = view.find_last_of('/');
    return slash == std::string_view::npos ? view : view.substr(slash + 1);
}

} // namespace

BasicFileSink::BasicFileSink(TimeZone timeZone) noexcept : timeZone_{timeZone} {}

BasicFileSink::~BasicFileSink() {
    closeFile();
}

void BasicFileSink::write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
    std::thread::id const& threadID, std::string_view message) {
    beforeWrite(timestamp);

    if (timestamp.tv_sec != cachedSecond_) {
        cachedSecond_ = timestamp.tv_sec;
        cachedDateTime_.clear();
        fmt::format_to(std::back_inserter(cachedDateTime_), "{:%Y-%m-%d %H:%M:%S}", toTm(timestamp.tv_sec));
    }

    line_.clear();
    fmt::format_to(std::back_inserter(line_), "{}.{:09} {} [{}] {}:{} {}\n",
        std::string_view{cachedDateTime_.data(), cachedDateTime_.size()}, timestamp.tv_nsec, logLevelName(level), threadID,
        baseName(location.file_name()), location.line(), message);
    auto const line = std::string_view{line_.data(), line_.size()};

    writeLine(line);
    if (level == LogLevel::Always) {
        // after writing: the file this message opened (if any) must not get it twice
        alwaysLines_.emplace_back(line);
    }
}

void BasicFileSink::flush() {
    if (file_ == nullptr) {
        return;
    }
    if (std::fflush(file_) != 0) {
        throwErrno("failed to flush log file");
    }
    if (::fdatasync(::fileno(file_)) != 0) {
        throwErrno("failed to sync log file");
    }
}

void BasicFileSink::openFile(std::filesystem::path const& path) {
    closeFile();

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    file_ = std::fopen(path.c_str(), "a");
    if (file_ == nullptr) {
        throwErrno("failed to open log file");
    }
    path_ = path;

    for (auto const& line : alwaysLines_) {
        writeLine(line);
    }
}

auto BasicFileSink::toTm(std::time_t seconds) const -> std::tm {
    std::tm tm{};
    auto const ok = timeZone_ == TimeZone::UTC ? ::gmtime_r(&seconds, &tm) : ::localtime_r(&seconds, &tm);
    if (ok == nullptr) {
        throwErrno("failed to convert log timestamp");
    }
    return tm;
}

auto BasicFileSink::fromTm(std::tm tm) const -> std::time_t {
    tm.tm_isdst = -1; // let mktime() work out daylight saving time
    return timeZone_ == TimeZone::UTC ? ::timegm(&tm) : std::mktime(&tm);
}

void BasicFileSink::writeLine(std::string_view line) {
    if (file_ == nullptr) {
        throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "no log file open"};
    }
    if (std::fwrite(line.data(), 1, line.size(), file_) != line.size()) {
        throwErrno("failed to write log file");
    }
}

void BasicFileSink::closeFile() noexcept {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace turboq::logger
