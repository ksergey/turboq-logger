// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "StdOutSink.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace turboq::logger {
namespace {

auto noColorRequested() noexcept -> bool {
    auto const* const value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
}

} // namespace

StdOutSink::StdOutSink(std::unique_ptr<FormatterBase> formatter)
    : formatter_{formatter ? std::move(formatter) : std::make_unique<DefaultFormatter>()} {
    formatter_->resolveAutoColors(::isatty(STDOUT_FILENO) == 1 && !noColorRequested());
}

void StdOutSink::write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
    std::thread::id const& threadID, std::string_view message) {
    line_.clear();
    formatter_->format(line_, location, level, timestamp, threadID, message);
    if (std::fwrite(line_.data(), 1, line_.size(), stdout) != line_.size()) [[unlikely]] {
        throw std::system_error{errno, std::generic_category(), "write to stdout failed"};
    }
}

void StdOutSink::flush() {
    if (std::fflush(stdout) != 0) [[unlikely]] {
        throw std::system_error{errno, std::generic_category(), "flush of stdout failed"};
    }
}

} // namespace turboq::logger
