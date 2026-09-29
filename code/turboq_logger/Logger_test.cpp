// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstring>
#include <string_view>

#include <doctest/doctest.h>

#include <turboq/SPSCMessageQueue.h>

#include "Logger.h"

namespace turboq_logger::testing {

TEST_SUITE("Logger") {

    TEST_CASE("version") {
        CHECK(!version().empty());
    }

    // Smoke test that the turboq dependency is wired up: headers are visible and the library links.
    TEST_CASE("turboq dependency") {
        auto result = turboq::SPSCMessageQueue::makeQueue("turboq-logger-test",
            turboq::SPSCMessageQueue::CreationOptions{.capacityHint = 1024 * 1024}, turboq::AnonymousMemorySource{});
        REQUIRE(result);

        auto queue = std::move(result).value();
        auto producer = queue.createProducer();
        auto consumer = queue.createConsumer();

        constexpr std::string_view message = "hello";
        auto buffer = producer.prepare(message.size());
        REQUIRE(buffer.size() >= message.size());
        std::memcpy(buffer.data(), message.data(), message.size());
        producer.commit();

        auto received = consumer.fetch();
        REQUIRE(received.size() == message.size());
        CHECK(std::string_view{reinterpret_cast<char const*>(received.data()), received.size()} == message);
        consumer.consume();
    }
}

} // namespace turboq_logger::testing
