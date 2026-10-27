// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "StdOutSink.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace turboq::logger {
namespace {

constexpr std::string_view kResetCode = "\x1b[0m";

/// The escape code fmt starts a style with (fmt only exposes styled output: strip its reset)
auto escapeCode(fmt::text_style const& style) -> std::string {
    auto code = fmt::format(style, "{}", "");
    if (std::string_view{code}.ends_with(kResetCode)) {
        code.resize(code.size() - kResetCode.size());
    }
    return code;
}

/// NO_COLOR set to a non-empty value forbids colors (https://no-color.org)
auto noColorRequested() noexcept -> bool {
    auto const* const value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
}

} // namespace

StdOutSink::StdOutSink(std::unique_ptr<FormatterBase> formatter, StdOutSinkOptions const& options)
    : colorMode_{options.colors},
      colorsEnabled_{options.colors == ColorMode::Always},
      formatter_{formatter ? std::move(formatter) : std::make_unique<DefaultFormatter>()} {
    if (colorMode_ == ColorMode::Auto) {
        colorsEnabled_ = ::isatty(STDOUT_FILENO) == 1 && !noColorRequested();
    }
    for (std::size_t i = 0; i < kLogLevelCount; ++i) {
        colorCodes_[i] = escapeCode(options.levelStyles[i]);
    }
}

void StdOutSink::write(std::source_location const& location, LogLevel level, ::timespec const& timestamp,
    std::thread::id const& threadID, std::string_view message) {
    auto const levelIndex = static_cast<std::size_t>(level);
    std::string_view const color =
        colorsEnabled_ && levelIndex < kLogLevelCount ? std::string_view{colorCodes_[levelIndex]} : std::string_view{};

    line_.clear();
    if (!color.empty()) {
        line_.append(color.data(), color.data() + color.size());
    }
    formatter_->format(line_, location, level, timestamp, threadID, message);
    if (!color.empty()) {
        // the formatter's '\n' trails the line: replace it with the reset and write it back, so
        // the style ends before the newline (the next line starts uncolored)
        line_.resize(line_.size() - 1);
        line_.append(kResetCode.data(), kResetCode.data() + kResetCode.size());
        line_.push_back('\n');
    }
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
