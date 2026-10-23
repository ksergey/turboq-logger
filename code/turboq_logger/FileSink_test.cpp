// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "FileSink.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <unistd.h>

#include <doctest/doctest.h>

#include "BackendThread.h"
#include "Logging.h"

namespace turboq::logger::testing {
namespace {

namespace fs = std::filesystem;

[[maybe_unused]] bool const utc = (::setenv("TZ", "UTC", 1), ::tzset(), true);

/// A fresh directory, removed with everything in it at the end of the scope
class TempDir {
private:
    fs::path path_;

public:
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "turboq-logger-test-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path_ = pattern;
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] auto path() const -> fs::path const& {
        return path_;
    }
};

constexpr auto kLocation = std::source_location::current();

void log(FileSink& sink, std::string_view message, LogLevel level = LogLevel::Info) {
    sink.write(kLocation, level, ::timespec{.tv_sec = 1792751045, .tv_nsec = 7}, std::this_thread::get_id(), message);
}

auto readFile(fs::path const& path) -> std::string {
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

/// The messages in a file: the text after the "<file>:<line> " field of each line
auto messagesIn(fs::path const& path) -> std::vector<std::string> {
    std::ifstream file{path};
    std::vector<std::string> messages;
    for (std::string line; std::getline(file, line);) {
        auto const at = line.find(".cpp:");
        REQUIRE(at != std::string::npos);
        messages.push_back(line.substr(line.find(' ', at) + 1));
    }
    return messages;
}

} // namespace

TEST_SUITE("FileSink") {

    TEST_CASE("the file is created on construction, parent directories included") {
        TempDir dir;
        auto const path = dir.path() / "a" / "b" / "app.log";
        FileSink sink{path};
        CHECK_EQ(sink.path(), path);
        CHECK(fs::exists(path));
        CHECK_EQ(fs::file_size(path), 0);
    }

    TEST_CASE("messages are written as LineFormatter lines, visible after flush()") {
        TempDir dir;
        FileSink sink{dir.path() / "app.log"};
        log(sink, "first", LogLevel::Warning);
        log(sink, "second");
        sink.flush();

        LineFormatter formatter;
        fmt::memory_buffer expected;
        ::timespec const ts{.tv_sec = 1792751045, .tv_nsec = 7};
        formatter.format(expected, kLocation, LogLevel::Warning, ts, std::this_thread::get_id(), "first");
        formatter.format(expected, kLocation, LogLevel::Info, ts, std::this_thread::get_id(), "second");
        CHECK_EQ(readFile(sink.path()), fmt::to_string(expected));
    }

    TEST_CASE("destruction flushes") {
        TempDir dir;
        auto const path = dir.path() / "app.log";
        {
            FileSink sink{path};
            log(sink, "unflushed");
        }
        CHECK_EQ(messagesIn(path), std::vector<std::string>{"unflushed"});
    }

    TEST_CASE("an existing file is appended to by default") {
        TempDir dir;
        auto const path = dir.path() / "app.log";
        {
            FileSink sink{path};
            log(sink, "run 1");
        }
        {
            FileSink sink{path};
            log(sink, "run 2");
        }
        CHECK_EQ(messagesIn(path), std::vector<std::string>{"run 1", "run 2"});
    }

    TEST_CASE("truncate starts with an empty file") {
        TempDir dir;
        auto const path = dir.path() / "app.log";
        {
            FileSink sink{path};
            log(sink, "old");
        }
        {
            FileSink sink{path, FileSinkOptions{.truncate = true}};
            CHECK_EQ(fs::file_size(path), 0);
            log(sink, "new");
        }
        CHECK_EQ(messagesIn(path), std::vector<std::string>{"new"});
    }

    TEST_CASE("a file that can't be opened fails on construction") {
        TempDir dir;
        std::ofstream{dir.path() / "file"} << "not a directory";
        CHECK_THROWS_AS(FileSink{dir.path() / "file" / "app.log"}, fs::filesystem_error);

        fs::create_directory(dir.path() / "dir.log"); // fopen() of a directory fails
        CHECK_THROWS_AS(FileSink{dir.path() / "dir.log"}, std::system_error);
    }

    TEST_CASE("a small buffer: lines longer than it are written whole") {
        TempDir dir;
        FileSink sink{dir.path() / "app.log", FileSinkOptions{.bufferSize = 16}};
        std::string const longMessage(1000, 'x');
        log(sink, longMessage);
        log(sink, "short");
        sink.flush();
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{longMessage, "short"});
    }

    TEST_CASE("fsyncOnFlush") {
        TempDir dir;
        FileSink sink{dir.path() / "app.log", FileSinkOptions{.fsyncOnFlush = true}};
        log(sink, "synced");
        CHECK_NOTHROW(sink.flush());
        CHECK_EQ(messagesIn(sink.path()), std::vector<std::string>{"synced"});
    }

    TEST_CASE("end to end: tq_* calls from several threads through the backend thread") {
        constexpr int kThreads = 4;
        constexpr int kMessages = 500;

        TempDir dir;
        auto const path = dir.path() / "app.log";
        BackendThread::setSink(std::make_shared<FileSink>(path, FileSinkOptions{.truncate = true}));
        BackendThread::start();
        {
            std::vector<std::jthread> threads;
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([t] {
                    for (int i = 0; i < kMessages; ++i) {
                        tq_info("thread {} message {}", t, i);
                    }
                });
            }
        }
        BackendThread::stop();
        BackendThread::setSink(nullptr);

        // every message, in order per thread
        auto const messages = messagesIn(path);
        CHECK_EQ(messages.size(), kThreads * kMessages);
        std::vector<int> next(kThreads, 0);
        bool inOrder = true;
        for (auto const& message : messages) {
            int t = 0;
            int i = 0;
            REQUIRE_EQ(std::sscanf(message.c_str(), "thread %d message %d", &t, &i), 2);
            inOrder = inOrder && i == next[t]++;
        }
        CHECK(inOrder);
    }
}

} // namespace turboq::logger::testing
