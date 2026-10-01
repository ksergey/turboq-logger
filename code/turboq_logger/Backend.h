// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <source_location>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/args.h>
#include <fmt/format.h>

#include <turboq/Platform.h>

#include "BoundedSPSCQueue.h"
#include "Codec.h"
#include "Common.h"
#include "Ordering.h"
#include "Sink.h"
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
    std::uint64_t sequence; // SequenceOrdering: the entry's sequence number; 0 otherwise
};

/// A decoded counter log entry
struct LogCounter {
    Timestamp timestamp;
    std::thread::id threadID;
    int counterID;
    LogCounterValue value;
    std::uint64_t sequence; // SequenceOrdering: the entry's sequence number; 0 otherwise
};

/// Something poll() hands decoded entries to: callable with LogMessage and with LogCounter
template <typename SinkFn>
concept LogSink = requires(SinkFn& sink, LogMessage const& message, LogCounter const& counter) {
    sink(message);
    sink(counter);
};

namespace detail {

/// Hands decoded entries to a Sink: messages with their timestamp as wall-clock time. Counters
/// aren't part of the Sink interface and are dropped.
struct SinkAdapter {
    Sink& sink;

    void operator()(LogMessage const& message) const {
        sink.write(message.location, message.level, Clock::toTimeSpec(message.timestamp), message.threadID, message.text);
    }

    void operator()(LogCounter const&) const noexcept {}
};

/// Backend state for merging queues: nothing unless the ordering merges
template <typename Ordering, bool = Ordering::kMerge>
struct MergeState {};

template <typename Ordering>
struct MergeState<Ordering, true> {
    /// The next unread entry of one queue
    struct Head {
        BoundedSPSCQueue::Consumer* consumer;
        std::span<std::byte const> buffer; // empty: nothing to read in this queue right now
        typename Ordering::Key key;
        std::size_t taken; // entries read from this queue in this poll()
    };

    std::vector<Head> heads;    // reused per poll()
    typename Ordering::Gate gate; // whether the smallest key may go now
};

} // namespace detail

/// Backend options: what a BasicBackend is parameterized with, like turboq's queue Options. Every
/// member is required:
///   Ordering   how entries of different threads are ordered: NoOrdering, TimestampOrdering or
///              SequenceOrdering (see Ordering.h)
template <typename Options>
concept BackendOptions = requires { typename Options::Ordering; } && LogOrdering<typename Options::Ordering>;

template <typename Options>
    requires BackendOptions<Options>
class BasicBackendThread;

/// What every backend shares, whatever its ordering: the global registry, the threads' contexts,
/// the queue size, and the one-backend-at-a-time rule. Not used on its own; see BasicBackend.
class BackendBase {
    // completes pending flushes when it stops, as nothing will flush them anymore
    template <typename Options>
        requires BackendOptions<Options>
    friend class BasicBackendThread;

protected:
    // Reused for every entry, so decoding and formatting don't allocate once they've grown
    fmt::dynamic_format_arg_store<fmt::format_context> args_;
    fmt::memory_buffer text_;

    /// Throws std::logic_error if another backend exists
    BackendBase();
    ~BackendBase();

    /// Decode the arguments at src with meta's decoder and format them into text_
    void formatMessage(LogEntryMessageMeta const& meta, std::byte const* src);

    /// Last flush ticket handed out by requestFlush()
    [[nodiscard]] static auto flushRequested() noexcept -> std::uint64_t;
    /// Last flush ticket the backend completed
    [[nodiscard]] static auto flushCompleted() noexcept -> std::uint64_t;
    /// Mark every ticket up to this one completed and wake their waiters
    static void completeFlush(std::uint64_t ticket) noexcept;

public:
    /// Identifies a flush request, see requestFlush()
    using FlushTicket = std::uint64_t;

    /// Queue size (bytes) until setQueueCapacity() is called
    static constexpr std::size_t kDefaultQueueCapacity = std::size_t{1} << 20;

    BackendBase(BackendBase const&) = delete;
    BackendBase& operator=(BackendBase const&) = delete;

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

    /// Ask the backend to flush its sink: on its next poll(Sink&) it reads every entry it can see --
    /// including all the calling thread logged before this call -- passes them to the sink and calls
    /// Sink::flush(). Doesn't wait. Thread-safe.
    static auto requestFlush() noexcept -> FlushTicket;

    /// Whether the flush with this ticket (and every earlier one) has been done. Thread-safe.
    [[nodiscard]] static auto isFlushed(FlushTicket ticket) noexcept -> bool;

    /// Block until the flush with this ticket has been done. Thread-safe.
    static void waitFlushed(FlushTicket ticket) noexcept;

    /// Request a flush and wait for it: once this returns, everything the calling thread logged
    /// before the call has gone through Sink::flush().
    ///
    /// Blocks until a backend handles it, so needs one polling with a Sink in another thread: never
    /// call it from the backend thread (or a sink), which would wait for itself.
    static void flush() noexcept;
};

/// Default backend options
struct BackendOptionsDefault {
    using Ordering = NoOrdering;
};

/// The logger backend: reads the queues of all threads and decodes their log entries, ordering
/// entries of different threads as Options::Ordering says (see Ordering.h). Pick the options with a
/// using:
///
///     struct MyBackendOptions {
///         using Ordering = SequenceOrdering;
///     };
///     using Backend = BasicBackend<MyBackendOptions>;
///
/// The frontend writing entries must use the same ordering, as it decides part of the entry layout:
/// take it from Backend::Ordering.
///
/// Frontend side (any thread), static: threadContext() gives the calling thread its ThreadContext
/// (thread_local), setQueueCapacity() sets the size of queues created from then on.
///
/// Backend side (one thread): an instance polls every queue, decodes each entry and hands it to a
/// sink:
///
///     Backend backend;
///     while (running) {
///         backend.poll(overloaded{
///             [](LogMessage const& message) { ... },
///             [](LogCounter const& counter) { ... },
///         });
///     }
///
/// Only one backend (of any ordering) may exist at a time: the registry allows a single thread to
/// visit consumers.
template <typename OptionsT>
    requires BackendOptions<OptionsT>
class BasicBackend : public BackendBase {
public:
    using Options = OptionsT;
    /// Ordering policy; the frontend must encode entries for the same one
    using Ordering = typename Options::Ordering;

private:
    [[no_unique_address]] detail::MergeState<Ordering> merge_;

public:
    /// Throws std::logic_error if another backend exists
    BasicBackend() = default;

    /// Read the queues of all threads, decode their entries and pass each to sink, in order per
    /// thread and, as Ordering says, across threads. Reads at most maxEntriesPerQueue entries from
    /// each queue, so one busy thread can't hold up the others; when merging, the poll stops at the
    /// first queue reaching it, as reading on would break the order. Return the number of entries
    /// processed.
    ///
    /// SequenceOrdering: stops early when the next number isn't visible yet (see
    /// SequenceOrdering::kGapTimeout); the next poll() carries on from there.
    ///
    /// An entry is consumed before it reaches the sink: an exception thrown by the sink propagates
    /// but doesn't leave the entry in the queue. A message whose arguments don't match its format
    /// string is passed on with an error text rather than thrown.
    template <typename SinkFn>
        requires LogSink<SinkFn>
    auto poll(SinkFn&& sink, std::size_t maxEntriesPerQueue = std::numeric_limits<std::size_t>::max())
        -> std::size_t {
        if constexpr (Ordering::kMerge) {
            return pollMerged(sink, maxEntriesPerQueue);
        } else {
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
    }

    /// poll() for a Sink: messages are passed to Sink::write() with a wall-clock timestamp (counters
    /// aren't part of the Sink interface and are dropped). Also handles flush requests (see
    /// BackendBase::flush()): after the regular pass, if one is pending, reads every entry it can
    /// see, then calls Sink::flush(). Return the number of entries processed.
    ///
    /// If the sink throws while flushing, pending flush requests are still marked done, so their
    /// waiters don't hang; the exception propagates.
    auto poll(Sink& sink, std::size_t maxEntriesPerQueue = std::numeric_limits<std::size_t>::max()) -> std::size_t {
        auto adapter = detail::SinkAdapter{sink};
        auto count = poll(adapter, maxEntriesPerQueue);
        // flushCompleted() is only written by the backend: comparing with it needs no ordering
        if (auto const requested = flushRequested(); requested != flushCompleted()) [[unlikely]] {
            // Everything logged before the requests up to `requested` is visible now (their
            // fetch_add released it, flushRequested() acquired it): drain it all, then flush.
            struct CompleteOnExit {
                std::uint64_t ticket;
                ~CompleteOnExit() {
                    completeFlush(ticket);
                }
            } const complete{requested};
            for (std::size_t n = poll(adapter); n != 0; n = poll(adapter)) {
                count += n;
            }
            sink.flush();
        }
        return count;
    }

private:
    /// poll() for merging policies: repeatedly pass on the entry with the smallest key among the
    /// heads of all queues
    template <typename SinkFn>
    auto pollMerged(SinkFn& sink, std::size_t maxEntriesPerQueue) -> std::size_t {
        std::size_t count = 0;
        using Head = typename detail::MergeState<Ordering>::Head;
        auto& heads = merge_.heads;

        registry().visitConsumers([&](std::span<BoundedSPSCQueue::Consumer> consumers) {
            heads.clear();
            for (auto& consumer : consumers) {
                auto& head = heads.emplace_back(Head{.consumer = &consumer, .buffer = {}, .key = {}, .taken = 0});
                refreshHead(head);
            }

            while (true) {
                Head* next = nullptr;
                for (auto& head : heads) {
                    if (!head.buffer.empty() && (next == nullptr || Ordering::before(head.key, next->key))) {
                        next = &head;
                    }
                }
                if (next == nullptr || next->taken == maxEntriesPerQueue) {
                    break;
                }
                // before processEntry(): a throwing sink can't make the gate count an entry twice
                if (!merge_.gate.accept(next->key)) {
                    break; // e.g. waiting for a missing sequence number
                }
                ++next->taken;
                ++count;
                processEntry(*next->consumer, next->buffer, sink);
                refreshHead(*next);
            }
        });
        return count;
    }

    /// Fetch the head of its queue and decode its key (a template: Head only exists when merging)
    static void refreshHead(auto& head) noexcept {
        head.buffer = head.consumer->fetch();
        if (!head.buffer.empty()) {
            auto src = head.buffer.data();
            auto const header = Codec<LogEntryHeader>::decode(src);
            head.key = Ordering::key(header, decodeEntryData<Ordering>(src));
        }
    }

    /// The entry's sequence number for LogMessage/LogCounter: 0 unless the policy has one
    [[nodiscard]] static auto sequenceOf(typename Ordering::EntryData const& data) noexcept -> std::uint64_t {
        if constexpr (requires { data.sequence; }) {
            return data.sequence;
        } else {
            return 0;
        }
    }

    /// Decode the entry in buffer, consume it, then pass it to sink
    template <typename SinkFn>
    void processEntry(BoundedSPSCQueue::Consumer& consumer, std::span<std::byte const> buffer, SinkFn& sink) {
        auto src = buffer.data();
        auto const header = Codec<LogEntryHeader>::decode(src);
        auto const sequence = sequenceOf(decodeEntryData<Ordering>(src));

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
                .sequence = sequence,
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
                .sequence = sequence,
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
};

/// The backend with default options: entries of different threads queue by queue
using Backend = BasicBackend<BackendOptionsDefault>;

} // namespace turboq::logger
