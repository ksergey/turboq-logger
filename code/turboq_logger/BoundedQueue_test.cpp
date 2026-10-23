// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BoundedQueue.h"

#include <cstdint>
#include <optional>
#include <thread>

#include <unistd.h>

#include <doctest/doctest.h>

#include <turboq/TestUtils.h>

namespace turboq::logger::testing {

using ::turboq::testing::dequeue;
using ::turboq::testing::enqueue;

TEST_SUITE("BoundedQueue") {

    struct Message {
        std::uint64_t seq;
    };

    auto makeQueue(std::size_t capacityHint = 1024 * 1024) -> BoundedQueue {
        auto result = BoundedQueue::makeQueue("test", BoundedQueue::CreationOptions{.capacityHint = capacityHint});
        REQUIRE(result);
        return std::move(result).value();
    }

    TEST_CASE("messages round-trip in order across many wraps") {
        auto queue = makeQueue(4096); // ~30 messages fit: 10000 of them wrap hundreds of times
        auto producer = queue.createProducer();
        auto consumer = queue.createConsumer();
        REQUIRE(producer);
        REQUIRE(consumer);
        REQUIRE_EQ(producer.capacity(), consumer.capacity());

        Message msg;
        for (std::uint64_t seq = 0; seq < 10000; ++seq) {
            REQUIRE(enqueue(producer, Message{.seq = seq}));
            REQUIRE(dequeue(consumer, msg));
            REQUIRE_EQ(msg.seq, seq);
        }
        REQUIRE_FALSE(dequeue(consumer, msg));
    }

    TEST_CASE("capacity is capacityHint rounded up to the page size, as for turboq::SPSCMessageQueue") {
        auto const pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));

        auto queue = makeQueue(3 * pageSize + 1);
        CHECK_EQ(queue.createProducer().capacity(), 4 * pageSize);
        CHECK_EQ(queue.createConsumer().capacity(), 4 * pageSize);

        auto const invalid = BoundedQueue::makeQueue("invalid", BoundedQueue::CreationOptions{.capacityHint = 0});
        REQUIRE_FALSE(invalid);
        CHECK_EQ(invalid.error(), makeErrorCode(Error::InvalidCreationOptions));
    }

    TEST_CASE("closed() turns true only after the producer is destroyed and the queue is drained") {
        auto queue = makeQueue();
        auto consumer = queue.createConsumer();
        CHECK_FALSE(consumer.closed()); // no producer yet: not closed, one may still come

        {
            auto producer = queue.createProducer();
            REQUIRE(enqueue(producer, Message{.seq = 1}));
            REQUIRE(enqueue(producer, Message{.seq = 2}));
            CHECK_FALSE(consumer.closed());
        }

        // producer is gone, but its messages are still readable
        Message msg;
        CHECK_FALSE(consumer.closed());
        REQUIRE(dequeue(consumer, msg));
        CHECK_EQ(msg.seq, 1);
        CHECK_FALSE(consumer.closed());
        REQUIRE(dequeue(consumer, msg));
        CHECK_EQ(msg.seq, 2);
        CHECK(consumer.closed());
    }

    TEST_CASE("a moved-from producer does not close the queue, assigning over a live one does") {
        auto queue = makeQueue();
        auto consumer = queue.createConsumer();

        std::optional<BoundedQueue::Producer> holder{queue.createProducer()};
        auto producer = std::move(*holder);
        holder.reset();
        CHECK_FALSE(consumer.closed());

        auto otherQueue = makeQueue();
        producer = otherQueue.createProducer();
        CHECK(consumer.closed());
    }

    TEST_CASE("consumer on another thread receives everything before seeing closed()") {
        constexpr std::uint64_t kMessageCount = 200000;

        auto queue = makeQueue(64 * 1024);
        auto consumer = queue.createConsumer();
        auto producer = queue.createProducer();

        std::thread producerThread{[producer = std::move(producer)]() mutable {
            for (std::uint64_t seq = 0; seq < kMessageCount;) {
                if (enqueue(producer, Message{.seq = seq})) {
                    ++seq;
                }
            }
        }}; // producer is destroyed when the thread finishes

        std::uint64_t expected = 0;
        bool inOrder = true;
        Message msg;
        while (!consumer.closed()) {
            if (dequeue(consumer, msg)) {
                inOrder = inOrder && msg.seq == expected;
                ++expected;
            }
        }
        producerThread.join();

        CHECK(inOrder);
        CHECK_EQ(expected, kMessageCount);
    }

    TEST_CASE("takeDropped() returns the drops counted since the previous call") {
        auto queue = makeQueue();
        auto producer = queue.createProducer();
        auto consumer = queue.createConsumer();

        CHECK_EQ(consumer.takeDropped(), 0);
        producer.countDropped();
        producer.countDropped();
        producer.countDropped();
        CHECK_EQ(consumer.takeDropped(), 3);
        CHECK_EQ(consumer.takeDropped(), 0); // already taken

        producer.countDropped();
        CHECK_EQ(consumer.takeDropped(), 1);
    }

    TEST_CASE("a consumer created later doesn't report drops counted before it existed") {
        auto queue = makeQueue();
        auto producer = queue.createProducer();
        producer.countDropped();

        auto consumer = queue.createConsumer();
        CHECK_EQ(consumer.takeDropped(), 0);
        producer.countDropped();
        CHECK_EQ(consumer.takeDropped(), 1);
    }

    TEST_CASE("drops counted on a full queue reach a consumer on another thread") {
        auto queue = makeQueue(4096);
        auto consumer = queue.createConsumer();
        auto producer = queue.createProducer();

        std::uint64_t enqueued = 0;
        std::uint64_t dropped = 0;
        std::thread{[&] {
            for (std::uint64_t seq = 0; seq < 1000; ++seq) {
                if (enqueue(producer, Message{.seq = seq})) {
                    ++enqueued;
                } else {
                    producer.countDropped();
                    ++dropped;
                }
            }
        }}.join();
        REQUIRE_GT(dropped, 0); // nothing consumed: the 4 KiB queue fills up

        std::uint64_t received = 0;
        Message msg;
        while (dequeue(consumer, msg)) {
            ++received;
        }
        CHECK_EQ(received, enqueued);
        CHECK_EQ(consumer.takeDropped(), dropped);
    }
}

} // namespace turboq::logger::testing
