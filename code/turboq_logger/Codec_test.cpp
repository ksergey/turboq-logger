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
}

} // namespace turboq::logger::testing
