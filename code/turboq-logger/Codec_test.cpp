// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <doctest/doctest.h>

namespace turboq::logger::testing {

TEST_SUITE("Codec") {

    TEST_CASE("trivially copyable round-trip") {
        struct Point {
            int x;
            double y;
        };

        std::array<std::byte, 64> buffer{};
        std::byte* dest = buffer.data();
        Codec<Point>::encode(dest, Point{.x = 42, .y = 3.5});
        CHECK_EQ(dest - buffer.data(), Codec<Point>::encodedSize());

        std::byte const* src = buffer.data();
        auto const value = Codec<Point>::decode(src);
        CHECK_EQ(src, dest);
        CHECK_EQ(value.x, 42);
        CHECK_EQ(value.y, 3.5);
    }

    TEST_CASE("string_view round-trip") {
        for (std::string_view const input : {std::string_view{}, std::string_view{"hello, turboq"}}) {
            std::array<std::byte, 64> buffer{};
            std::byte* dest = buffer.data();
            Codec<std::string_view>::encode(dest, input);
            CHECK_EQ(dest - buffer.data(), Codec<std::string_view>::encodedSize(input));

            std::byte const* src = buffer.data();
            CHECK_EQ(Codec<std::string_view>::decode(src), input);
            CHECK_EQ(src, dest);
        }
    }

    TEST_CASE("values of different types are decoded back in order") {
        std::array<std::byte, 64> buffer{};
        std::byte* dest = buffer.data();
        Codec<std::uint64_t>::encode(dest, 7);
        Codec<std::string_view>::encode(dest, "abc");
        Codec<char>::encode(dest, 'z');

        std::byte const* src = buffer.data();
        CHECK_EQ(Codec<std::uint64_t>::decode(src), 7u);
        CHECK_EQ(Codec<std::string_view>::decode(src), "abc");
        CHECK_EQ(Codec<char>::decode(src), 'z');
        CHECK_EQ(src, dest);
    }

    TEST_CASE("a string longer than the size field can hold is truncated, consistently in size and encoding") {
        using StringCodec = Codec<std::string_view>;

        // only size() is looked at: no 4 GiB buffer needed
        char const dummy = 'x';
        auto const huge = std::string_view{&dummy, StringCodec::kMaxSize + 100};
        CHECK_EQ(StringCodec::encodedLength(huge), StringCodec::kMaxSize);
        CHECK_EQ(StringCodec::encodedSize(huge), sizeof(StringCodec::SizeType) + StringCodec::kMaxSize);

        auto const exact = std::string_view{&dummy, StringCodec::kMaxSize};
        CHECK_EQ(StringCodec::encodedLength(exact), StringCodec::kMaxSize);

        CHECK_EQ(StringCodec::encodedLength("abc"), 3);
    }

    TEST_CASE("an empty string encodes as just its size, whatever its data pointer") {
        std::array<std::byte, 16> buffer{};
        std::byte* dest = buffer.data();
        Codec<std::string_view>::encode(dest, std::string_view{}); // data() == nullptr
        Codec<std::string_view>::encode(dest, std::string_view{"abc", 0});
        CHECK_EQ(dest - buffer.data(), 2 * sizeof(std::uint32_t));

        std::byte const* src = buffer.data();
        CHECK(Codec<std::string_view>::decode(src).empty());
        CHECK(Codec<std::string_view>::decode(src).empty());
    }
}

} // namespace turboq::logger::testing
