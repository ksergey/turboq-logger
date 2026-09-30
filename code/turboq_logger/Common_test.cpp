// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Common.h"

#include <array>
#include <cstring>
#include <source_location>
#include <thread>

#include <doctest/doctest.h>

namespace turboq::logger::testing {

TEST_SUITE("Common") {

    TEST_CASE("log entry header and meta pointer round-trip through a byte buffer") {
        static constexpr auto location = std::source_location::current();
        static constexpr auto meta = LogEntryMessageMeta{
            .location = &location, .level = LogLevel::Notice, .format = "{}", .decodeArgs = nullptr};

        auto const header = LogEntryHeader{
            .timestamp = {}, .threadID = std::this_thread::get_id(), .type = LogEntryType::Message};
        auto const* const metaPtr = &meta;

        // layout: | LogEntryHeader | LogEntryMessageMeta* | Args... |
        alignas(LogEntryHeader) std::array<std::byte, sizeof(LogEntryHeader) + sizeof(metaPtr)> buffer{};
        std::memcpy(buffer.data(), &header, sizeof(header));
        std::memcpy(buffer.data() + sizeof(header), &metaPtr, sizeof(metaPtr));

        LogEntryHeader decodedHeader;
        LogEntryMessageMeta const* decodedMeta = nullptr;
        std::memcpy(&decodedHeader, buffer.data(), sizeof(decodedHeader));
        std::memcpy(&decodedMeta, buffer.data() + sizeof(decodedHeader), sizeof(decodedMeta));

        CHECK_EQ(decodedHeader.threadID, std::this_thread::get_id());
        CHECK_EQ(decodedHeader.type, LogEntryType::Message);
        REQUIRE_EQ(decodedMeta, &meta);
        CHECK_EQ(decodedMeta->level, LogLevel::Notice);
        CHECK_EQ(decodedMeta->location->line(), location.line());
    }
}

} // namespace turboq::logger::testing
