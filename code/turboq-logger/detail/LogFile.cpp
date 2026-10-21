// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "LogFile.h"

#include <cerrno>
#include <string>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace turboq::logger::detail {
namespace {

[[noreturn]] void throwError(char const* what, std::filesystem::path const& path) {
    throw std::system_error{errno, std::generic_category(), std::string{what} + " log file " + path.string()};
}

} // namespace

void LogFile::FileCloser::operator()(std::FILE* file) const noexcept {
    std::fclose(file);
}

LogFile::LogFile(std::filesystem::path path, Mode mode, std::size_t bufferSize) : path_{std::move(path)} {
    if (auto const parent = path_.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // "e": O_CLOEXEC, so child processes don't inherit the log file
    file_.reset(std::fopen(path_.c_str(), mode == Mode::Append ? "ae" : "we"));
    if (!file_) {
        throwError("can't open", path_);
    }
    if (std::setvbuf(file_.get(), nullptr, _IOFBF, bufferSize) != 0) {
        throwError("can't set the buffer of", path_);
    }
}

void LogFile::write(std::string_view data) {
    if (std::fwrite(data.data(), 1, data.size(), file_.get()) != data.size()) [[unlikely]] {
        // the failed write sets the stream's error indicator, which would make every following
        // fwrite() fail without touching the file (e.g. after a one-off disk-full or I/O error).
        // Clear it before throwing, so the next write() retries for real.
        ::clearerr(file_.get());
        throwError("can't write to", path_);
    }
}

void LogFile::flush(bool sync) {
    if (std::fflush(file_.get()) != 0) [[unlikely]] {
        ::clearerr(file_.get()); // as in write(): let a later flush() retry
        throwError("can't flush", path_);
    }
    if (sync && ::fsync(::fileno(file_.get())) != 0) [[unlikely]] {
        throwError("can't fsync", path_);
    }
}

} // namespace turboq::logger::detail
