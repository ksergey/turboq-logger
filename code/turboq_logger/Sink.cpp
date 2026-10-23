// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Sink.h"

#include <iterator>

#include <fmt/format.h>

namespace turboq::logger {

void Sink::dropped(std::uint64_t count) {
    static constexpr auto location = std::source_location::current();

    ::timespec now{};
    ::clock_gettime(CLOCK_REALTIME, &now);

    fmt::memory_buffer message;
    fmt::format_to(std::back_inserter(message), "turboq-logger: {} message(s) dropped, queue full", count);
    this->write(location, LogLevel::Warning, now, std::thread::id{}, std::string_view{message.data(), message.size()});
}

} // namespace turboq::logger
