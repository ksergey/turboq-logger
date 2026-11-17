[![C++](https://img.shields.io/badge/C++-23-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-orange)]()
[![License](https://img.shields.io/github/license/ksergey/turboq-logger)](LICENSE)
[![CMake](https://img.shields.io/badge/build-CMake-informational.svg)](https://cmake.org)
[![CI](https://github.com/ksergey/turboq-logger/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/ksergey/turboq-logger/actions/workflows/build-and-test.yml)

> turboq-logger is a low-latency C++ logging library built on top of [turboq](https://github.com/ksergey/turboq).

## Quick Start

### Dependencies

- C++23 compiler (CI covers GCC 14+ and Clang 20+)
- CMake 3.24+

Everything else is fetched automatically at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)
(the first configure needs network access):

- [turboq](https://github.com/ksergey/turboq) -- message queues (tracks `master`)
- [fmt](https://github.com/fmtlib/fmt) -- formatting
- [doctest](https://github.com/doctest/doctest) -- unit tests (`code/turboq-logger/*_test.cpp`); skipped when
  `-Dturboq_logger_BUILD_TESTS=OFF`
- [Google Benchmark](https://github.com/google/benchmark) -- benchmarks (`code/turboq-logger/*_bm.cpp`); skipped
  when `-Dturboq_logger_BUILD_BENCHMARKS=OFF`

Set `CPM_SOURCE_CACHE` to share downloaded dependencies between build directories and projects.

### Integration

```cmake
include(cmake/GetCPM.cmake)
CPMAddPackage("gh/ksergey/turboq-logger#master")

target_link_libraries(your_app PRIVATE turboq::logger)
```

`turboq::logger` links `turboq::turboq` publicly, so turboq headers are available to your code as well.

### Usage

```cpp
#include <turboq-logger/DailyFileSink.h>
#include <turboq-logger/Logging.h>

using namespace turboq::logger;

BackendThread::setSink(std::make_shared<DailyFileSink>("logs/app")); // logs/app_20261023.log, ...

setLogLevel(LogLevel::Info);                                       // tq_debug() and below: skipped
tq_info("order {} filled at {}", orderID, price);

BackendThread::flush(); // wait until this thread's messages are written and the sink flushed
BackendThread::stop();  // optional: done at exit
```

- Sinks: `StdOutSink` (stdout), `FileSink` (one file, appended to or truncated), `DailyFileSink`
  (`<prefix>_YYYYMMDD.log`, a new file at local midnight), `NullSink`; or derive your own from
  `Sink`. The text sinks take a `FormatterBase` (see below).
- Each thread logs to its own bounded queue. When it is full, the message is dropped and counted;
  the count is printed to stderr when the queue is destroyed.
- An exception thrown by the sink skips that one message: it is reported on stderr (at most once
  a second) and the backend carries on.

### Line format

The text sinks format each message with a `FormatterBase`; `Formatter<Pattern>` takes its pattern
as a template argument: an fmt format string with named fields, checked and compiled at compile
time. The default pattern gives

```
2026-10-23 13:24:05.123456789 info    [140367476504512] main.cpp:42 order 7 filled at 101.25
```

```cpp
// a pattern of your own
BackendThread::setSink(std::make_shared<StdOutSink>(
    makeFormatter<"{time:%T}.{ms:03} {level:>7} {file}:{line} {message}">()));
```

| Field | Value | Format spec examples |
|---|---|---|
| `{time}` | `std::tm`, local time | any fmt chrono spec: `{time:%F %T}`, `{time:%H:%M}` |
| `{utc}` | `std::tm`, UTC | same |
| `{ns}` `{us}` `{ms}` | fraction of the second | `{ns:09}`, `{us:06}`, `{ms:03}` |
| `{level}` | `info`, `warning`... | `{level:<7}`, `{level:>7}` |
| `{thread}` | thread ID, an integer | `{thread:x}` |
| `{file}` / `{path}` | file name / full path of the call site | `{file:>20}` |
| `{line}` | line of the call site | `{line:<4}` |
| `{function}` | function of the call site | |
| `{message}` | the formatted message | |

- A bad pattern doesn't compile: an unknown field (`unknown_field_in_log_line_pattern`), an
  unbalanced brace (`unmatched_brace_in_log_line_pattern`), a bad format spec (fmt's own check).
- Literal braces are doubled: `{{` and `}}`. A `'\n'` ends every line.
- The sinks take a `std::unique_ptr<FormatterBase>`; `nullptr` (the default) is `DefaultFormatter`.
  Derive from `FormatterBase` for a format of your own.
- At compile time the names become positional arguments of an fmt compiled format, and the time
  fields are formatted once per second: the default pattern takes ~185 ns a line on the backend
  thread.

### Benchmarks

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
taskset -c 2 ./build-release/code/turboq-logger-Logging-bm
```

`Logging_bm.cpp` measures the cost of a log call on the calling thread: the level check, the
timestamp and encoding the arguments into the thread's queue (formatting happens on the backend).
`Formatter_bm.cpp` measures the backend's formatting of a line, by pattern.

### License

Distributed under the MIT License. See LICENSE for details.
