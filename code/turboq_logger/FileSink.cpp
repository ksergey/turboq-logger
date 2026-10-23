// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "FileSink.h"

#include <utility>

namespace turboq::logger {

FileSink::FileSink(std::filesystem::path path, FileSinkOptions const& options)
    : file_{std::move(path), options.truncate ? detail::LogFile::Mode::Truncate : detail::LogFile::Mode::Append,
          options.bufferSize},
      fsyncOnFlush_{options.fsyncOnFlush} {}

void FileSink::write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
    std::thread::id const& threadID, std::string_view message) {
    line_.clear();
    formatter_.format(line_, location, level, timestamp, threadID, message);
    file_.write(std::string_view{line_.data(), line_.size()});
}

void FileSink::flush() {
    file_.flush(fsyncOnFlush_);
}

} // namespace turboq::logger
