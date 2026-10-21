// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT
//
// turboq-logger usage example: configure a sink once for the whole application, then log from any
// thread. The backend thread starts by itself on the first message and drains the queues and
// flushes the sink when the program ends.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <unistd.h>

#include <turboq_logger/BackendThread.h>
#include <turboq_logger/Codec.h>
#include <turboq_logger/DailyFileSink.h>
#include <turboq_logger/Transform.h>

// ---------------------------------------------------------------------------------------------------
// A minimal frontend, for this example only: what the library's log macro and function will do.
// The macro gives every call site its own static meta (location, level, format, argument decoder);
// the function encodes the entry into the calling thread's queue. Neither knows about the sink.
// ---------------------------------------------------------------------------------------------------

namespace example {

using namespace turboq::logger;

/// Type an argument is encoded as (strings as std::string_view, enums as their underlying type...)
template <typename T>
using Encoded = std::remove_cvref_t<decltype(transform(std::declval<T const&>()))>;

/// Decodes the arguments of one call site's entries for formatting in the backend
template <typename... Args>
void decodeArgs(std::byte const* src, fmt::dynamic_format_arg_store<fmt::format_context>* store) {
    (store->push_back(Codec<Args>::decode(src)), ...); // comma fold: left to right
}

/// metaFn returns the call site's meta for the given encoded argument types
template <typename MetaFn, typename... Args>
void log(MetaFn, Args const&... args) {
    using Ordering = BackendThread::Ordering; // entries must match the backend's ordering
    BackendThread::ensureStarted();           // starts the backend thread on the first message

    auto const* const meta = MetaFn{}.template operator()<Encoded<Args>...>();
    auto& context = BackendBase::threadContext(); // the calling thread's queue
    auto& producer = context.producer();

    auto const size = sizeof(LogEntryHeader) + kEntryDataSize<Ordering> + sizeof(meta) +
                      (std::size_t{0} + ... + Codec<Encoded<Args>>::encodedSize(transform(args)));
    auto const buffer = producer.prepare(size);
    if (buffer.empty()) {
        return; // queue full: this message is dropped
    }
    auto dest = buffer.data();
    Codec<LogEntryHeader>::encode(dest,
        LogEntryHeader{.timestamp = Clock::now(), .threadID = context.threadID(), .type = LogEntryType::Message});
    encodeEntryData<Ordering>(dest, Ordering::makeEntryData()); // after prepare(): see nextLogSequence()
    Codec<LogEntryMessageMeta const*>::encode(dest, meta);
    (Codec<Encoded<Args>>::encode(dest, transform(args)), ...);
    producer.commit();
}

} // namespace example

/// EXAMPLE_LOG(LogLevel::Notice, "{} + {} = {}", 1, 2, 3)
///
/// The parameters are LEVEL and FORMAT, not level and format: the preprocessor would substitute them
/// into the designated initializers .level and .format too.
#define EXAMPLE_LOG(LEVEL, FORMAT, ...)                                                                    \
    ::example::log(                                                                                        \
        []<typename... ExampleArgs>() noexcept -> ::turboq::logger::LogEntryMessageMeta const* {           \
            static constexpr auto location = std::source_location::current();                             \
            static constexpr auto meta = ::turboq::logger::LogEntryMessageMeta{.location = &location,      \
                .level = (LEVEL),                                                                          \
                .format = (FORMAT),                                                                        \
                .decodeArgs = &::example::decodeArgs<ExampleArgs...>};                                     \
            return &meta;                                                                                  \
        } __VA_OPT__(, ) __VA_ARGS__)

// ---------------------------------------------------------------------------------------------------
// The application
// ---------------------------------------------------------------------------------------------------

int main() {
    using turboq::logger::BackendThread;
    using turboq::logger::DailyFileSink;
    using turboq::logger::LogLevel;

    // Once, at startup: where the whole application's messages go. A new file every day; each file
    // starts with the Always messages logged so far (like the first one below).
    auto const sink = std::make_shared<DailyFileSink>("example-logs/example-{:%Y-%m-%d}.log");
    BackendThread::setSink(sink);

    EXAMPLE_LOG(LogLevel::Always, "example started, pid {}", ::getpid());

    std::vector<std::jthread> workers;
    for (int worker = 0; worker < 3; ++worker) {
        workers.emplace_back([worker] {
            for (int step = 0; step < 2; ++step) {
                EXAMPLE_LOG(LogLevel::Notice, "worker {} step {}", worker, step);
            }
        });
    }
    workers.clear(); // join

    EXAMPLE_LOG(LogLevel::Warning, "all workers done in {}", std::filesystem::current_path());

    // Wait until everything logged so far is written and flushed, to show the file here. Not needed
    // before exiting: the end of the program drains and flushes too.
    BackendThread::flush();
    std::cout << "--- " << sink->path().string() << '\n' << std::ifstream{sink->path()}.rdbuf();
}
