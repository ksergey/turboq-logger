// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <type_traits>

#include "Clock.h"
#include "Common.h"

namespace turboq::logger {

// Ordering policies: how the backend orders log entries of different threads (entries of one thread
// are always in order). Picked as the Ordering member of the backend options (see BackendOptions in
// Backend.h).
//
// A policy owns the part of the entry layout ordering needs: its EntryData is written right after
// LogEntryHeader (see Common.h), so the frontend writing entries and the backend reading them must
// use the same policy -- one per program.
//
// Policy interface:
//   EntryData                     trivially copyable; empty if the policy needs nothing in the entry
//   makeEntryData()               EntryData for an entry being written; call after prepare() succeeded
//   kMerge                        whether the backend merges queues (false: queue by queue)
//   Key, key(header, data)        what entries are merged by (kMerge only)
//   before(a, b)                  strict order of keys (kMerge only)
//   Gate                          backend-side state: gate.accept(key) says whether the entry with the
//                                 smallest visible key may be passed on now (kMerge only)

/// Queue by queue, no ordering across threads (cheapest)
struct NoOrdering {
    struct EntryData {};

    static constexpr bool kMerge = false;

    [[nodiscard]] static constexpr auto makeEntryData() noexcept -> EntryData {
        return {};
    }
};

namespace detail {

/// Strict "a was logged before b" for both Timestamp representations (TSC ticks, timespec)
[[nodiscard]] constexpr auto timestampBefore(std::int64_t a, std::int64_t b) noexcept -> bool {
    return a < b;
}

[[nodiscard]] constexpr auto timestampBefore(::timespec const& a, ::timespec const& b) noexcept -> bool {
    return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
}

inline std::atomic<std::uint64_t> logSequence{0};

} // namespace detail

/// Merged by timestamp, best effort: an entry timestamped earlier but committed later than one
/// already passed on arrives out of order. With the wall clock (no TSC), clock adjustments distort
/// the order across threads too.
struct TimestampOrdering {
    struct EntryData {};

    static constexpr bool kMerge = true;

    using Key = Timestamp;

    [[nodiscard]] static constexpr auto makeEntryData() noexcept -> EntryData {
        return {};
    }

    [[nodiscard]] static constexpr auto key(LogEntryHeader const& header, EntryData const&) noexcept -> Key {
        return header.timestamp;
    }

    [[nodiscard]] static constexpr auto before(Key const& a, Key const& b) noexcept -> bool {
        return detail::timestampBefore(a, b);
    }

    struct Gate {
        [[nodiscard]] constexpr auto accept(Key const&) noexcept -> bool {
            return true;
        }
    };
};

/// Next number of the global log sequence (SequenceOrdering)
///
/// Take it only after the queue's prepare() has succeeded, right before writing the entry: the
/// backend passes entries on in exact sequence order, so a number taken for an entry that is then
/// never committed (e.g. queue full) leaves a gap it has to wait out.
[[nodiscard]] inline auto nextLogSequence() noexcept -> std::uint64_t {
    // relaxed: the counter only has to hand out unique numbers in one total order (its modification
    // order); the entries themselves are published by the queue
    return detail::logSequence.fetch_add(1, std::memory_order_relaxed);
}

/// Merged by a global atomic sequence number: the exact order of the log calls. Costs 8 bytes per
/// entry and an atomic increment per log call, on a counter shared by all threads.
struct SequenceOrdering {
    struct EntryData {
        std::uint64_t sequence;
    };

    static constexpr bool kMerge = true;

    using Key = std::uint64_t;

    /// How long the backend waits for a missing sequence number while later ones are available
    /// before skipping it. Normally a number is missing only for the moment between being taken and
    /// its entry being committed; this bounds the wait if a frontend breaks the nextLogSequence()
    /// contract. An entry that turns up after its number was skipped is passed on as soon as seen.
    static constexpr std::chrono::milliseconds kGapTimeout{100};

    /// Takes the entry's sequence number: call only after prepare() succeeded
    [[nodiscard]] static auto makeEntryData() noexcept -> EntryData {
        return {.sequence = nextLogSequence()};
    }

    [[nodiscard]] static constexpr auto key(LogEntryHeader const&, EntryData const& data) noexcept -> Key {
        return data.sequence;
    }

    [[nodiscard]] static constexpr auto before(Key a, Key b) noexcept -> bool {
        return a < b;
    }

    class Gate {
    private:
        // The number expected next. Static, so a new backend carries on where the previous one
        // stopped (only one backend exists at a time).
        static inline std::uint64_t nextSequence_ = 0;
        // Since when the expected number has been missing while later ones are available
        std::chrono::steady_clock::time_point gapSince_{};
        bool inGap_{false};

    public:
        /// Whether the entry with this (smallest visible) number may be passed on now, and if so
        /// account for it -- so call it right before passing the entry on
        [[nodiscard]] auto accept(Key sequence) -> bool {
            if (sequence > nextSequence_) {
                // some smaller number isn't visible yet: wait for it, but not forever
                auto const now = std::chrono::steady_clock::now();
                if (!inGap_) {
                    inGap_ = true;
                    gapSince_ = now;
                }
                if (now - gapSince_ < kGapTimeout) {
                    return false;
                }
            }
            if (sequence >= nextSequence_) {
                // in order, or skipping a gap that timed out
                nextSequence_ = sequence + 1;
                inGap_ = false;
            }
            // else: late entry whose number was skipped -- pass it on right away
            return true;
        }
    };
};

template <typename Ordering>
concept LogOrdering = requires {
    typename Ordering::EntryData;
    { Ordering::makeEntryData() } -> std::same_as<typename Ordering::EntryData>;
    { Ordering::kMerge } -> std::convertible_to<bool>;
} && std::is_trivially_copyable_v<typename Ordering::EntryData>;

static_assert(LogOrdering<NoOrdering>);
static_assert(LogOrdering<TimestampOrdering>);
static_assert(LogOrdering<SequenceOrdering>);

/// Size of Ordering's EntryData in an entry: 0 when empty
template <typename Ordering>
inline constexpr std::size_t kEntryDataSize =
    std::is_empty_v<typename Ordering::EntryData> ? 0 : sizeof(typename Ordering::EntryData);

/// Write Ordering's EntryData at dest (nothing when empty) and advance dest
template <typename Ordering>
void encodeEntryData(std::byte*& dest, typename Ordering::EntryData const& data) noexcept {
    if constexpr (kEntryDataSize<Ordering> != 0) {
        std::memcpy(dest, &data, sizeof(data));
        dest += sizeof(data);
    }
}

/// Read Ordering's EntryData at src (nothing when empty) and advance src
template <typename Ordering>
[[nodiscard]] auto decodeEntryData(std::byte const*& src) noexcept -> typename Ordering::EntryData {
    typename Ordering::EntryData data{};
    if constexpr (kEntryDataSize<Ordering> != 0) {
        std::memcpy(&data, src, sizeof(data));
        src += sizeof(data);
    }
    return data;
}

} // namespace turboq::logger
