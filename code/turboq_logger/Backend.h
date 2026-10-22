// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <thread>

#include <fmt/args.h>
#include <fmt/format.h>

#include <turboq/Platform.h>

#include "Clock.h"
#include "Codec.h"
#include "LogLevel.h"
#include "ThreadContext.h"
#include "ThreadQueueRegistry.h"
#include "TransitEvent.h"

namespace turboq::logger {

struct LogMessageEvent {
    Clock::Timestamp timestamp;
    std::thread::id threadID;
    LogLevel level;
    std::source_location const& location;
    std::string_view message;
};

struct LogValueEvent {
    Clock::Timestamp timestamp;
    std::thread::id threadID;
    unsigned id;
    double value;
};

template <typename SinkFn>
concept EventSink = requires(SinkFn& sink, LogMessageEvent const& logMessageEvent, LogValueEvent& logValueEvent) {
    sink(logMessageEvent);
    sink(logValueEvent);
};

class Backend final {
private:
    fmt::dynamic_format_arg_store<fmt::format_context> argsStore_;
    fmt::memory_buffer messageBuffer_;

public:
    Backend(Backend const&) = delete;
    Backend& operator=(Backend const&) = delete;

    /// Construct backend
    /// \throw std::logic_error on backend already exists
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

    /// Size (bytes) queues are created
    [[nodiscard]] static auto queueCapacity() -> std::size_t;

    /// Set queue capacity hint
    /// \throw std::invalid_argument if capacity is 0.
    static void setQueueCapacity(std::size_t capacity);

    /// Read the queues of all threads, decode their entries and pass each to sink
    template <typename Sink>
        requires EventSink<Sink>
    auto poll(Sink&& sink) -> std::size_t {
        std::size_t processedCount = 0;

        registry().forEachConsumer([&](BoundedQueue::Consumer& consumer) {
            while (true) {
                auto buffer = consumer.fetch();
                if (buffer.empty()) {
                    break;
                }
                processBuffer(buffer, sink);
                consumer.consume();
                ++processedCount;
            }
        });

        return processedCount;
    }

private:
    void formatMessage(MessageMetadata const& meta, std::byte const* src);

    template <typename Sink>
    void processBuffer(std::span<std::byte const> buffer, Sink& sink) {
        auto src = buffer.data();
        auto const header = Codec<TransitEvent>::decode(src);

        switch (header.type) {
        case TransitEventType::Message: {
            auto const meta = Codec<MessageMetadata const*>::decode(src);
            // format message into messageBuffer_
            formatMessage(*meta, src);
            sink(LogMessageEvent{
                .timestamp = header.timestamp,
                .threadID = header.threadID,
                .level = meta->level,
                .location = *meta->location,
                .message = std::string_view{messageBuffer_.data(), messageBuffer_.size()},
            });
        } break;
        case TransitEventType::Value: {
            auto const meta = Codec<ValueMetadata const*>::decode(src);
            auto const value = Codec<double>::decode(src);
            sink(LogValueEvent{
                .timestamp = header.timestamp,
                .threadID = header.threadID,
                .id = meta->id,
                .value = value,
            });
        } break;
        default: {
            assert(false && "unknown transit event type");
        } break;
        }
    }
};

} // namespace turboq::logger
