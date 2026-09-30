// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "ThreadContext.h"

#include <cstdint>
#include <thread>
#include <type_traits>

#include <doctest/doctest.h>

#include <turboq/TestUtils.h>

namespace turboq::logger::testing {

using ::turboq::testing::dequeue;
using ::turboq::testing::enqueue;

static_assert(!std::is_convertible_v<ThreadQueueRegistry&, ThreadContext>);

TEST_SUITE("ThreadContext") {

    struct Message {
        std::uint64_t seq;
    };

    TEST_CASE("remembers its thread and writes to a queue of the registry") {
        ThreadQueueRegistry registry{BoundedSPSCQueue::CreationOptions{.capacityHint = 4096}};

        std::thread::id threadID;
        std::thread::id contextThreadID;
        std::thread{[&] {
            ThreadContext context{registry};
            threadID = std::this_thread::get_id();
            contextThreadID = context.threadID();
            CHECK(enqueue(context.producer(), Message{.seq = 7})); // not REQUIRE: throws off-thread
        }}.join();
        CHECK_EQ(contextThreadID, threadID);

        // the context is gone: its message is still delivered, then the consumer is dropped
        Message msg{};
        auto const left = registry.forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
            REQUIRE(dequeue(consumer, msg));
        });
        CHECK_EQ(msg.seq, 7);
        CHECK_EQ(left, 0);
    }
}

} // namespace turboq::logger::testing
