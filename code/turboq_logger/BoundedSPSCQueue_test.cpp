// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "BoundedSPSCQueue.h"

#include <cstdint>
#include <optional>
#include <thread>

#include <unistd.h>

#include <doctest/doctest.h>

#include <turboq/SPSCMessageQueue.h>
#include <turboq/TestUtils.h>

namespace turboq::logger::testing {

using ::turboq::testing::dequeue;
using ::turboq::testing::enqueue;
using ::turboq::testing::MemorySourceFixture;

TEST_SUITE("BoundedSPSCQueue") {

    struct Message {
        std::uint64_t seq;
    };

    auto makeQueue(std::size_t capacityHint = 1024 * 1024) -> BoundedSPSCQueue {
        auto result = BoundedSPSCQueue::makeQueue(
            "test", BoundedSPSCQueue::CreationOptions{.capacityHint = capacityHint}, AnonymousMemorySource{});
        REQUIRE(result);
        return std::move(result).value();
    }

    TEST_CASE("defaults to anonymous memory") {
        // no memory source given: nothing is created on disk or in /dev/shm, and each queue is a
        // separate anonymous file, so the same name never refers to the same queue
        auto first = BoundedSPSCQueue::makeQueue(
            "anonymous", BoundedSPSCQueue::CreationOptions{.capacityHint = 4096});
        REQUIRE(first);
        auto second = BoundedSPSCQueue::makeQueue(
            "anonymous", BoundedSPSCQueue::CreationOptions{.capacityHint = 8192});
        REQUIRE(second); // a named queue would fail here with SizeMismatch

        CHECK_FALSE(BoundedSPSCQueue::makeQueue("anonymous")); // open-only never finds it
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

        auto const invalid = BoundedSPSCQueue::makeQueue(
            "invalid", BoundedSPSCQueue::CreationOptions{.capacityHint = 0}, AnonymousMemorySource{});
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

        std::optional<BoundedSPSCQueue::Producer> holder{queue.createProducer()};
        auto producer = std::move(*holder);
        holder.reset();
        CHECK_FALSE(consumer.closed());

        auto otherQueue = makeQueue();
        producer = otherQueue.createProducer();
        CHECK(consumer.closed());
    }

    TEST_CASE_FIXTURE(MemorySourceFixture, "a new producer re-opens a closed queue") {
        auto const memorySource = makeTempMemorySource();
        auto const options = BoundedSPSCQueue::CreationOptions{.capacityHint = 8192};

        auto consumer = BoundedSPSCQueue::makeConsumer("reopen", options, memorySource);
        REQUIRE(consumer);

        {
            // separate handle, as if from another process
            auto producer = BoundedSPSCQueue::makeProducer("reopen", memorySource);
            REQUIRE(producer);
        }
        CHECK(consumer->closed());

        auto producer = BoundedSPSCQueue::makeProducer("reopen", memorySource);
        REQUIRE(producer);
        CHECK_FALSE(consumer->closed());

        Message msg;
        REQUIRE(enqueue(*producer, Message{.seq = 7}));
        REQUIRE(dequeue(*consumer, msg));
        CHECK_EQ(msg.seq, 7);
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

    TEST_CASE_FIXTURE(MemorySourceFixture, "errors") {
        auto const memorySource = makeTempMemorySource();
        auto const options = BoundedSPSCQueue::CreationOptions{.capacityHint = 8192};

        auto queue = BoundedSPSCQueue::makeQueue("errors", options, memorySource);
        REQUIRE(queue);

        SUBCASE("only one producer and one consumer") {
            auto producer = queue->createProducer();
            auto consumer = queue->createConsumer();
            CHECK_THROWS_AS((void)queue->createProducer(), std::system_error);
            CHECK_THROWS_AS((void)queue->createConsumer(), std::system_error);
            CHECK_FALSE(BoundedSPSCQueue::makeProducer("errors", memorySource));
            CHECK_FALSE(BoundedSPSCQueue::makeConsumer("errors", memorySource));
        }

        SUBCASE("capacity mismatch") {
            auto const other = BoundedSPSCQueue::makeQueue(
                "errors", BoundedSPSCQueue::CreationOptions{.capacityHint = 16384}, memorySource);
            REQUIRE_FALSE(other);
            CHECK_EQ(other.error(), makeErrorCode(Error::SizeMismatch));
        }

        SUBCASE("opening a queue that doesn't exist") {
            CHECK_FALSE(BoundedSPSCQueue::makeQueue("missing", memorySource));
        }

        SUBCASE("a plain turboq SPSC queue is not a BoundedSPSCQueue") {
            auto const spsc = ::turboq::SPSCMessageQueue::makeQueue(
                "plain", ::turboq::SPSCMessageQueue::CreationOptions{.capacityHint = 8192}, memorySource);
            REQUIRE(spsc);
            auto const other = BoundedSPSCQueue::makeQueue("plain", memorySource);
            REQUIRE_FALSE(other);
            CHECK_EQ(other.error(), makeErrorCode(Error::TagMismatch));
        }
    }
}

} // namespace turboq::logger::testing
