// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstddef>
#include <limits>
#include <source_location>
#include <string_view>
#include <thread>

#include <fmt/args.h>
#include <fmt/format.h>

#include <turboq/Platform.h>

#include "BoundedSPSCQueue.h"
#include "Codec.h"
#include "Common.h"
#include "ThreadContext.h"
#include "ThreadQueueRegistry.h"

namespace turboq::logger {

/// A decoded message log entry. Only valid during the sink call: text points into a buffer the
/// backend reuses for the next entry.
struct LogMessage {
    Timestamp timestamp;
    std::thread::id threadID;
    LogLevel level;
    std::source_location const& location;
    std::string_view text;
};

/// A decoded counter log entry
struct LogCounter {
    Timestamp timestamp;
    std::thread::id threadID;
    int counterID;
    LogCounterValue value;
};

/// Something poll() hands decoded entries to: callable with LogMessage and with LogCounter
template <typename Sink>
concept LogSink = requires(Sink& sink, LogMessage const& message, LogCounter const& counter) {
    sink(message);
    sink(counter);
};

/// The logger backend: reads the queues of all threads and decodes their log entries.
///
/// Frontend side (any thread), static:
///   - threadContext() gives the calling thread its ThreadContext (thread_local): the producer of its
///     own queue, created in the global registry on first use and closed when the thread exits
///   - setQueueCapacity() sets the size of queues created from then on
///
/// Backend side (one thread): a Backend instance polls every queue, decodes each entry and hands it
/// to a sink:
///
///     Backend backend;
///     while (running) {
///         backend.poll(overloaded{
///             [](LogMessage const& message) { ... },
///             [](LogCounter const& counter) { ... },
///         });
///     }
///
/// Only one Backend may exist at a time: the registry allows a single thread to visit consumers.
/// Entries of one thread arrive in order; entries of different threads are not merged by timestamp.
class Backend {
private:
    // Reused for every entry, so decoding and formatting don't allocate once they've grown
    fmt::dynamic_format_arg_store<fmt::format_context> args_;
    fmt::memory_buffer text_;

public:
    /// Queue size (bytes) until setQueueCapacity() is called
    static constexpr std::size_t kDefaultQueueCapacity = std::size_t{1} << 20;

    Backend(Backend const&) = delete;
    Backend& operator=(Backend const&) = delete;

    /// Throws std::logic_error if another Backend exists
    Backend();

    ~Backend();

    /// The registry holding every thread's queue
    [[nodiscard]] static auto registry() -> ThreadQueueRegistry&;

    /// The calling thread's context, created (with a new queue) on the first call from this thread
    /// and destroyed when the thread exits, which closes the queue.
    /// Throws std::system_error if the queue couldn't be created (first call only).
    [[nodiscard]] TURBOQ_FORCE_INLINE static auto threadContext() -> ThreadContext& {
        thread_local ThreadContext context{registry()};
        return context;
    }

    /// Size (bytes, rounded up to the page size) of queues created from now on, i.e. of threads
    /// that haven't logged yet; queues already created keep their size. Thread-safe.
    /// Throws std::invalid_argument if capacity is 0.
    static void setQueueCapacity(std::size_t capacity);

    /// Size (bytes) queues are created with. Thread-safe.
    [[nodiscard]] static auto queueCapacity() -> std::size_t;

    /// Read the queues of all threads, decode their entries and pass each to sink, in order per
    /// thread. Reads at most maxEntriesPerQueue entries from each queue, so one busy thread can't
    /// hold up the others. Return the number of entries processed.
    ///
    /// An entry is consumed before it reaches the sink: an exception thrown by the sink propagates
    /// but doesn't leave the entry in the queue. A message whose arguments don't match its format
    /// string is passed on with an error text rather than thrown.
    template <typename Sink>
        requires LogSink<Sink>
    auto poll(Sink&& sink, std::size_t maxEntriesPerQueue = std::numeric_limits<std::size_t>::max())
        -> std::size_t {
        std::size_t count = 0;
        registry().forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
            for (std::size_t n = 0; n < maxEntriesPerQueue; ++n) {
                auto const buffer = consumer.fetch();
                if (buffer.empty()) {
                    break;
                }
                processEntry(consumer, buffer, sink);
                ++count;
            }
        });
        return count;
    }

private:
    /// Decode the entry in buffer, consume it, then pass it to sink
    template <typename Sink>
    void processEntry(BoundedSPSCQueue::Consumer& consumer, std::span<std::byte const> buffer, Sink& sink) {
        auto src = buffer.data();
        auto const header = Codec<LogEntryHeader>::decode(src);

        switch (header.type) {
        case LogEntryType::Message: {
            auto const meta = Codec<LogEntryMessageMeta const*>::decode(src);
            formatMessage(*meta, src); // text_ owns the result: the entry can go
            consumer.consume();
            sink(LogMessage{
                .timestamp = header.timestamp,
                .threadID = header.threadID,
                .level = meta->level,
                .location = *meta->location,
                .text = std::string_view{text_.data(), text_.size()},
            });
            break;
        }
        case LogEntryType::Counter: {
            auto const meta = Codec<LogEntryCounterMeta const*>::decode(src);
            auto const value = Codec<LogCounterValue>::decode(src);
            consumer.consume();
            sink(LogCounter{
                .timestamp = header.timestamp,
                .threadID = header.threadID,
                .counterID = meta->counterID,
                .value = value,
            });
            break;
        }
        default:
            // corrupt or unknown entry: nothing to decode, just drop it
            assert(false && "unknown log entry type");
            consumer.consume();
            break;
        }
    }

    /// Decode the arguments at src with meta's decoder and format them into text_
    void formatMessage(LogEntryMessageMeta const& meta, std::byte const* src);
};

} // namespace turboq::logger
