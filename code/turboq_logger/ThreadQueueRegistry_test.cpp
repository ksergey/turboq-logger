// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "ThreadQueueRegistry.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include <turboq/TestUtils.h>

namespace turboq::logger::testing {

using ::turboq::testing::dequeue;
using ::turboq::testing::enqueue;

TEST_SUITE("ThreadQueueRegistry") {

    struct Message {
        std::uint32_t thread;
        std::uint64_t seq;
    };

    auto const kOptions = BoundedSPSCQueue::CreationOptions{.capacityHint = 64 * 1024};

    TEST_CASE("every createProducer() call creates a new queue") {
        ThreadQueueRegistry registry{kOptions};

        auto first = registry.createProducer();
        auto second = registry.createProducer();
        REQUIRE(first);
        REQUIRE(second);
        CHECK_EQ(registry.forEachConsumer([](auto&) {}), 2);

        // independent queues: each message arrives on exactly one consumer
        REQUIRE(enqueue(first, Message{.thread = 1, .seq = 10}));
        REQUIRE(enqueue(second, Message{.thread = 2, .seq = 20}));
        std::map<std::uint32_t, std::uint64_t> received;
        registry.forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
            Message msg;
            REQUIRE(dequeue(consumer, msg));
            received[msg.thread] = msg.seq;
            CHECK_FALSE(dequeue(consumer, msg));
        });
        CHECK_EQ(received, std::map<std::uint32_t, std::uint64_t>{{1, 10}, {2, 20}});
    }

    TEST_CASE("a consumer is dropped only once its producer is destroyed and its queue is drained") {
        ThreadQueueRegistry registry{kOptions};

        std::optional<BoundedSPSCQueue::Producer> producer{registry.createProducer()};
        REQUIRE(enqueue(*producer, Message{.thread = 1, .seq = 1}));
        REQUIRE(enqueue(*producer, Message{.thread = 1, .seq = 2}));
        CHECK_EQ(registry.forEachConsumer([](auto&) {}), 1);

        // producer is gone, but its messages aren't read yet: the consumer stays
        producer.reset();
        CHECK_EQ(registry.forEachConsumer([](auto&) {}), 1);

        std::vector<std::uint64_t> received;
        auto const left = registry.forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
            Message msg;
            while (dequeue(consumer, msg)) {
                received.push_back(msg.seq);
            }
        });
        CHECK_EQ(received, std::vector<std::uint64_t>{1, 2});
        CHECK_EQ(left, 0);
    }

    TEST_CASE("a thread_local producer closes its queue when the thread exits") {
        ThreadQueueRegistry registry{kOptions};

        auto log = [&](std::uint64_t seq) {
            thread_local auto producer = registry.createProducer();
            return enqueue(producer, Message{.thread = 0, .seq = seq});
        };

        std::thread{[&] {
            CHECK(log(1)); // not REQUIRE: throws off-thread
            CHECK(log(2)); // same thread-local producer, same queue
        }}.join();

        std::vector<std::uint64_t> received;
        auto const left = registry.forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
            Message msg;
            while (dequeue(consumer, msg)) {
                received.push_back(msg.seq);
            }
        });
        CHECK_EQ(received, std::vector<std::uint64_t>{1, 2});
        CHECK_EQ(left, 0);
    }

    TEST_CASE("backend receives everything from threads that come and go while it runs") {
        constexpr std::uint32_t kThreadCount = 8;
        constexpr std::uint64_t kMessagesPerThread = 50000;

        ThreadQueueRegistry registry{kOptions};

        std::atomic<std::uint32_t> finished{0};
        std::vector<std::thread> threads;
        for (std::uint32_t thread = 0; thread < kThreadCount; ++thread) {
            threads.emplace_back([&, thread] {
                {
                    auto producer = registry.createProducer();
                    for (std::uint64_t seq = 0; seq < kMessagesPerThread;) {
                        if (enqueue(producer, Message{.thread = thread, .seq = seq})) {
                            ++seq;
                        }
                    }
                } // producer destroyed: queue closed
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        // per thread: next expected sequence number; any gap or reordering shows up as a mismatch
        std::map<std::uint32_t, std::uint64_t> expected;
        bool inOrder = true;
        std::size_t left = 0;
        do {
            left = registry.forEachConsumer([&](BoundedSPSCQueue::Consumer& consumer) {
                Message msg;
                while (dequeue(consumer, msg)) {
                    inOrder = inOrder && msg.seq == expected[msg.thread]++;
                }
            });
        } while (finished.load(std::memory_order_acquire) < kThreadCount || left > 0);

        for (auto& thread : threads) {
            thread.join();
        }

        CHECK(inOrder);
        REQUIRE_EQ(expected.size(), kThreadCount);
        for (auto const& [thread, count] : expected) {
            CHECK_EQ(count, kMessagesPerThread);
        }
    }
}

} // namespace turboq::logger::testing
