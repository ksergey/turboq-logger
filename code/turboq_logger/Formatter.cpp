// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Formatter.h"

#include <bit>
#include <type_traits>

#include <pthread.h>

namespace turboq::logger {
namespace {

// fmt prints std::thread::id through an ostringstream (~190 ns); it wraps a pthread_t, which
// prints the same number as an integer (~30 ns)
static_assert(sizeof(std::thread::id) == sizeof(::pthread_t));
static_assert(std::is_trivially_copyable_v<std::thread::id>);
static_assert(std::is_integral_v<::pthread_t> && sizeof(::pthread_t) <= sizeof(std::uint64_t));

auto threadNumber(std::thread::id const& threadID) noexcept -> std::uint64_t {
    return std::bit_cast<::pthread_t>(threadID);
}

constexpr std::string_view kResetCode = "\x1b[0m";

/// The escape code fmt starts a style with (fmt only exposes styled output: strip its reset)
auto escapeCode(fmt::text_style const& style) -> std::string {
    auto code = fmt::format(style, "{}", "");
    if (std::string_view{code}.ends_with(kResetCode)) {
        code.resize(code.size() - kResetCode.size());
    }
    return code;
}

void append(fmt::memory_buffer& out, std::string_view text) {
    out.append(text.data(), text.data() + text.size());
}

} // namespace

FormatterBase::FormatterBase(FormatterOptions const& options)
    : colorMode_{options.colors}, colorsEnabled_{options.colors == ColorMode::Always} {
    for (std::size_t i = 0; i < kLogLevelCount; ++i) {
        colorCodes_[i] = escapeCode(options.levelStyles[i]);
    }
}

void FormatterBase::resolveAutoColors(bool enabled) noexcept {
    if (colorMode_ == ColorMode::Auto) {
        colorsEnabled_ = enabled;
    }
}

void FormatterBase::format(fmt::memory_buffer& out, std::source_location const& location, LogLevel level,
    ::timespec const& timestamp, std::thread::id const& threadID, std::string_view message) {
    auto const levelIndex = static_cast<std::size_t>(level);
    std::string_view const color =
        colorsEnabled_ && levelIndex < kLogLevelCount ? std::string_view{colorCodes_[levelIndex]} : std::string_view{};
    append(out, color);

    auto const path = std::string_view{location.file_name()};
    this->formatLine(out, LineFields{
                              .timestamp = timestamp,
                              .level = logLevelToString(level),
                              .thread = threadNumber(threadID),
                              .file = baseName(path),
                              .path = path,
                              .line = location.line(),
                              .function = location.function_name(),
                              .message = message,
                          });

    if (!color.empty()) {
        append(out, kResetCode); // before the '\n': the next line starts uncolored
    }
    out.push_back('\n');
}

} // namespace turboq::logger
