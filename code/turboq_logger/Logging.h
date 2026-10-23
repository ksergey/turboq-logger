// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <fmt/args.h>
#include <fmt/format.h>

#include <turboq/Platform.h>

#include "BackendThread.h"
#include "Clock.h"
#include "Codec.h"
#include "Transform.h"
#include "TransitEvent.h"

namespace turboq::logger {
namespace detail {

/// Decodes the arguments of one call site's entries for formatting in the backend
template <typename... Args>
void decodeArgs(
    [[maybe_unused]] std::byte const* src, [[maybe_unused]] fmt::dynamic_format_arg_store<fmt::format_context>* store) {
    (store->push_back(Codec<Args>::decode(src)), ...); // comma fold: left to right
}

} // namespace detail

template <typename M, typename... Args>
TURBOQ_FORCE_INLINE void logTransformedArgsImpl(Args const&... args) {
    // starts the backend thread on the first message
    BackendThread::ensureStarted();

    // Check format-string in compile time
    [[maybe_unused]] static constexpr auto formatStringCheck = fmt::format_string<Args...>(M::format());

    // Obtain clock time as soon as possible
    auto const now = Clock::now();

    static constexpr auto meta = MessageMetadata{
        .location = M::location(),
        .level = M::level(),
        .format = M::format(),
        .decode = &detail::decodeArgs<Args...>,
    };

    auto& context = Backend::threadContext();
    auto& producer = context.producer();

    auto const size = Codec<TransitEvent>::encodedSize() + Codec<MessageMetadata const*>::encodedSize() +
                      (std::size_t{0} + ... + Codec<Args>::encodedSize(args));
    auto const buffer = producer.prepare(size);
    if (buffer.empty()) [[unlikely]] {
        producer.countDropped(); // queue full: reported on stderr when the queue is destroyed
        return;
    }
    auto dest = buffer.data();
    Codec<TransitEvent>::encode(
        dest, TransitEvent{.type = TransitEventType::Message, .timestamp = now, .threadID = context.threadID()});
    Codec<MessageMetadata const*>::encode(dest, &meta);
    // Encode args
    (Codec<Args>::encode(dest, args), ...);
    producer.commit();
}

template <typename M, typename... Args>
TURBOQ_FORCE_INLINE void logImpl(Args const&... args) {
    return logTransformedArgsImpl<M>(transform(args)...);
}

} // namespace turboq::logger

#define TURBOQ_LOGGER_LOG_CALL(LEVEL, FORMAT, ...)                                                                     \
    do {                                                                                                               \
        static constexpr auto locationStore = std::source_location::current();                                         \
        struct {                                                                                                       \
            static constexpr auto location() noexcept -> std::source_location const* {                                 \
                return &locationStore;                                                                                 \
            }                                                                                                          \
            static constexpr auto level() noexcept -> ::turboq::logger::LogLevel {                                     \
                return (LEVEL);                                                                                        \
            }                                                                                                          \
            static constexpr auto format() noexcept -> std::string_view {                                              \
                return std::string_view(FORMAT);                                                                       \
            }                                                                                                          \
        } infoStore;                                                                                                   \
        ::turboq::logger::logImpl<decltype(infoStore)>(__VA_ARGS__);                                                   \
    } while (0)

#define TURBOQ_LOGGER_LOG(LEVEL, FORMAT, ...)                                                                          \
    if (::turboq::logger::isLogLevelEnabled((LEVEL))) {                                                                \
        TURBOQ_LOGGER_LOG_CALL(LEVEL, FORMAT __VA_OPT__(, ) __VA_ARGS__);                                              \
    }

#define tq_error(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::Error, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_warn(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::Warning, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_notice(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::Notice, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_info(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::Info, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_debug(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::Debug, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_trace(FORMAT, ...) TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::TraceL1, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_trace_l1(FORMAT, ...)                                                                                       \
    TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::TraceL1, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_trace_l2(FORMAT, ...)                                                                                       \
    TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::TraceL2, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
#define tq_trace_l3(FORMAT, ...)                                                                                       \
    TURBOQ_LOGGER_LOG(::turboq::logger::LogLevel::TraceL3, (FORMAT)__VA_OPT__(, ) __VA_ARGS__)
