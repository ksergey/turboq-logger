// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <fmt/args.h>

#include "Clock.h"

namespace turboq::logger {

/// Log entry verbosity level
enum class LogLevel { Always, Error, Warning, Notice, Debug, Trace };

/// Name of a log level, e.g. "NOTICE". Not called toString(): test frameworks (doctest) and other
/// libraries use that name as an ADL customization point with their own return type.
[[nodiscard]] constexpr auto logLevelName(LogLevel level) noexcept -> std::string_view {
    switch (level) {
    case LogLevel::Always:
        return "ALWAYS";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Notice:
        return "NOTICE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Trace:
        return "TRACE";
    }
    return "UNKNOWN";
}

/// Decode args function signature
using DecodeArgsFn = std::add_pointer_t<void(std::byte const*, fmt::dynamic_format_arg_store<fmt::format_context>*)>;

/// Format a log entry from a buffer into a string
using FormatFn = std::add_pointer_t<std::string(std::byte const*)>;

/// Log entry message meta
struct LogEntryMessageMeta {
    /// Log entry source location
    std::source_location const* location;
    /// Log entry verbosity level
    LogLevel level;
    /// Format string
    std::string_view format;
    /// Function to decode log entry args from a buffer
    DecodeArgsFn decodeArgs;
};
static_assert(std::is_trivially_copyable_v<LogEntryMessageMeta>);

/// Log entry counter meta
struct LogEntryCounterMeta {
    int counterID;
};

/// Type of the counter value in a counter log entry (see Layout below)
using LogCounterValue = std::int64_t;

/// Log entry type
enum LogEntryType { Message, Counter };

/// Log entry header
struct LogEntryHeader {
    /// Log event timestamp
    Timestamp timestamp;
    /// Log event source thread id
    std::thread::id threadID;
    /// Log entry type
    LogEntryType type;
};
static_assert(std::is_trivially_copyable_v<LogEntryHeader>);

/// Layout
///   v1: | LogEntryHeader{type = LogEntryType::Message} | EntryData | LogEntryMessageMeta* | Args... |
///   v2: | LogEntryHeader{type = LogEntryType::Counter} | EntryData | LogEntryCounterMeta* | LogCounterValue |
///
/// EntryData belongs to the ordering policy (see Ordering.h) and is empty unless it needs data in
/// the entry (SequenceOrdering: the sequence number).

} // namespace turboq::logger
