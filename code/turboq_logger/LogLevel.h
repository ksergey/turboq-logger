// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace turboq::logger {

enum class LogLevel : std::uint8_t {
    Error,
    Warning,
    Notice,
    Info,
    Debug,
    TraceL1,
    TraceL2,
    TraceL3,
};

constexpr auto logLevelToString(LogLevel value) noexcept -> std::string_view {
    using namespace std::string_view_literals;

    switch (value) {
    case LogLevel::Error: return "error"sv;
    case LogLevel::Warning: return "warning"sv;
    case LogLevel::Notice: return "notice"sv;
    case LogLevel::Info: return "info"sv;
    case LogLevel::Debug: return "debug"sv;
    case LogLevel::TraceL1: return "tracel1"sv;
    case LogLevel::TraceL2: return "tracel2"sv;
    case LogLevel::TraceL3: return "tracel3"sv;
    default: break;
    }
    return "?"sv;
}

constexpr auto logLevelFromString(std::string_view value) noexcept -> std::optional<LogLevel> {
    using namespace std::string_view_literals;

    if (value == "error"sv || value == "err"sv) {
        return LogLevel::Error;
    } else if (value == "warning"sv || value == "warn"sv) {
        return LogLevel::Warning;
    } else if (value == "notice"sv) {
        return LogLevel::Notice;
    } else if (value == "info"sv) {
        return LogLevel::Info;
    } else if (value == "debug"sv) {
        return LogLevel::Debug;
    } else if (value == "tracel1"sv || value == "trace_l1"sv || value == "trace"sv) {
        return LogLevel::TraceL1;
    } else if (value == "tracel2"sv || value == "trace_l2"sv) {
        return LogLevel::TraceL2;
    } else if (value == "tracel3"sv || value == "trace_l3"sv) {
        return LogLevel::TraceL3;
    }
    return std::nullopt;
}

} // namespace turboq::logger
