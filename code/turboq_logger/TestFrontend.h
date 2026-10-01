// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Test-only: a minimal frontend encoding log entries the way the real one will, for tests of the
// backend side.

#include <cstddef>
#include <thread>

#include <fmt/args.h>

#include "Backend.h"
#include "Codec.h"
#include "Common.h"
#include "Ordering.h"

namespace turboq::logger::testing {

template <typename... Args>
void decodeArgs(std::byte const* src, fmt::dynamic_format_arg_store<fmt::format_context>* store) {
    (store->push_back(Codec<Args>::decode(src)), ...); // comma fold: decoded left to right
}

/// Write the parts every entry starts with: | LogEntryHeader | EntryData |. Call only after
/// prepare() succeeded: with SequenceOrdering it takes the entry's sequence number.
template <typename Ordering>
void encodeEntryStart(std::byte*& dest, LogEntryType type, Timestamp timestamp) {
    Codec<LogEntryHeader>::encode(
        dest, LogEntryHeader{.timestamp = timestamp, .threadID = std::this_thread::get_id(), .type = type});
    encodeEntryData<Ordering>(dest, Ordering::makeEntryData());
}

template <typename Ordering, typename... Args>
auto logMessageAt(Timestamp timestamp, LogEntryMessageMeta const& meta, Args const&... args) -> bool {
    auto& producer = BackendBase::threadContext().producer();
    auto const size = sizeof(LogEntryHeader) + kEntryDataSize<Ordering> + sizeof(LogEntryMessageMeta const*) +
                      (std::size_t{0} + ... + Codec<Args>::encodedSize(args));
    auto buffer = producer.prepare(size);
    if (buffer.empty()) {
        return false;
    }
    auto dest = buffer.data();
    encodeEntryStart<Ordering>(dest, LogEntryType::Message, timestamp);
    Codec<LogEntryMessageMeta const*>::encode(dest, &meta);
    (Codec<Args>::encode(dest, args), ...);
    producer.commit();
    return true;
}

template <typename Ordering, typename... Args>
auto logMessage(LogEntryMessageMeta const& meta, Args const&... args) -> bool {
    return logMessageAt<Ordering>(Clock::now(), meta, args...);
}

template <typename Ordering>
auto logCounter(LogEntryCounterMeta const& meta, LogCounterValue value) -> bool {
    auto& producer = BackendBase::threadContext().producer();
    auto buffer = producer.prepare(
        sizeof(LogEntryHeader) + kEntryDataSize<Ordering> + sizeof(LogEntryCounterMeta const*) + sizeof(value));
    if (buffer.empty()) {
        return false;
    }
    auto dest = buffer.data();
    encodeEntryStart<Ordering>(dest, LogEntryType::Counter, Clock::now());
    Codec<LogEntryCounterMeta const*>::encode(dest, &meta);
    Codec<LogCounterValue>::encode(dest, value);
    producer.commit();
    return true;
}

} // namespace turboq::logger::testing
