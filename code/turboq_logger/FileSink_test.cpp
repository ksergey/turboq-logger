// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "FileSink.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

namespace turboq::logger::testing {
namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        auto pattern = (std::filesystem::temp_directory_path() / "turboq-logger-test-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path = pattern;
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

auto messagesIn(std::filesystem::path const& path) -> std::vector<std::string> {
    std::ifstream in{path};
    std::vector<std::string> messages;
    for (std::string line; std::getline(in, line);) {
        messages.push_back(line.substr(line.rfind(' ') + 1));
    }
    return messages;
}

/// Any file sink: switches to the next numbered file whenever asked to. Only decides *when* to open
/// which file -- replaying Always messages is the base class's job.
class NumberedFileSink final : public BasicFileSink {
public:
    std::filesystem::path directory;
    int fileNumber = 0;
    bool switchRequested = true;

    explicit NumberedFileSink(std::filesystem::path dir) : BasicFileSink{TimeZone::UTC}, directory{std::move(dir)} {}

private:
    void beforeWrite(::timespec const&) override {
        if (switchRequested) {
            switchRequested = false;
            openFile(directory / ("file" + std::to_string(++fileNumber) + ".log"));
        }
    }
};

void write(Sink& sink, LogLevel level, std::string_view message) {
    sink.write(std::source_location::current(), level, ::timespec{.tv_sec = 1790000000, .tv_nsec = 0},
        std::this_thread::get_id(), message);
}

using Messages = std::vector<std::string>;

} // namespace

TEST_SUITE("BasicFileSink") {

    TEST_CASE("every file a file sink opens starts with all Always messages so far, in order") {
        TempDir dir;
        {
            NumberedFileSink sink{dir.path};
            write(sink, LogLevel::Always, "one"); // opens file1
            write(sink, LogLevel::Debug, "x");
            write(sink, LogLevel::Always, "two");
            sink.switchRequested = true;
            write(sink, LogLevel::Error, "y"); // opens file2
            sink.switchRequested = true;
            write(sink, LogLevel::Always, "three"); // opens file3, then written once
        }
        CHECK_EQ(messagesIn(dir.path / "file1.log"), Messages{"one", "x", "two"});
        CHECK_EQ(messagesIn(dir.path / "file2.log"), Messages{"one", "two", "y"});
        CHECK_EQ(messagesIn(dir.path / "file3.log"), Messages{"one", "two", "three"});
    }

    TEST_CASE("a file that can't be opened is reported") {
        TempDir dir;
        std::filesystem::create_directory(dir.path / "taken");
        struct BadSink final : BasicFileSink {
            std::filesystem::path path;
            explicit BadSink(std::filesystem::path p) : BasicFileSink{TimeZone::UTC}, path{std::move(p)} {}
            void beforeWrite(::timespec const&) override {
                openFile(path); // a directory: can't be opened as a file
            }
        } sink{dir.path / "taken"};
        CHECK_THROWS_AS(write(sink, LogLevel::Notice, "x"), std::system_error);
    }

    TEST_CASE("level names") {
        CHECK_EQ(logLevelName(LogLevel::Always), "ALWAYS");
        CHECK_EQ(logLevelName(LogLevel::Error), "ERROR");
        CHECK_EQ(logLevelName(LogLevel::Warning), "WARNING");
        CHECK_EQ(logLevelName(LogLevel::Notice), "NOTICE");
        CHECK_EQ(logLevelName(LogLevel::Debug), "DEBUG");
        CHECK_EQ(logLevelName(LogLevel::Trace), "TRACE");
    }
}

} // namespace turboq::logger::testing
