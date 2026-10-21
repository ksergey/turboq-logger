// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>

#include <fmt/args.h>

#include "Clock.h"
#include "LogLevel.h"

namespace turboq::logger {

enum class TransitEventType {
    Message,
    Value,
};

struct TransitEvent {
    /// Event type
    TransitEventType type;
    /// Transition event timestamp
    Clock::Timestamp timestamp;
    /// Thread identifier
    std::thread::id threadID;
    // TODO: sequence?
};

static_assert(std::is_trivially_copyable_v<TransitEvent>);

/// Decode format args function signature
using DecodeFn = std::add_pointer_t<void(std::byte const*, fmt::dynamic_format_arg_store<fmt::format_context>*)>;

struct MessageMetadata {
    /// Message source location
    std::source_location const* location;
    /// Log level
    LogLevel level;
    /// Message format
    std::string_view format;
    /// Decode arguments function
    DecodeFn decode;
};

static_assert(std::is_trivially_copyable_v<MessageMetadata>);

struct ValueMetadata {
    /// Value identifier
    unsigned id;
    // TODO: value type (int, double, etc)?
};

static_assert(std::is_trivially_copyable_v<ValueMetadata>);

} // namespace turboq::logger
