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
- [doctest](https://github.com/doctest/doctest) -- unit tests (`code/turboq_logger/*_test.cpp`); skipped when
  `-Dturboq_logger_BUILD_TESTS=OFF`

Set `CPM_SOURCE_CACHE` to share downloaded dependencies between build directories and projects.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `turboq_logger_TSC_CLOCK` | `ON` on x86-64, `OFF` elsewhere | Timestamp log entries with the TSC (`rdtsc`, x86-64 only; needs an invariant TSC) instead of `clock_gettime(CLOCK_REALTIME)`. Defines `TURBOQ_LOGGER_TSC_CLOCK` publicly, so code linking `turboq::logger` sees the same choice. Turning it on for another architecture is a configure error. |
| `turboq_logger_BUILD_TESTS` | `ON` | Build `*_test.cpp` files as doctest executables and register them with `ctest`. |

```bash
cmake -B build -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
ctest --test-dir build/code --output-on-failure
```

To develop against a local turboq checkout instead of the fetched one:

```bash
cmake -B build -DCPM_turboq_SOURCE=/path/to/turboq
```

### Integration

```cmake
include(cmake/GetCPM.cmake)
CPMAddPackage(
    NAME turboq-logger
    GITHUB_REPOSITORY ksergey/turboq-logger
    GIT_TAG master
    OPTIONS "turboq_logger_BUILD_TESTS OFF")

target_link_libraries(your_app PRIVATE turboq::logger)
```

`turboq::logger` links `turboq::turboq` publicly, so turboq headers are available to your code as well.

### Usage

```cpp
#include <turboq_logger/Logging.h>

using namespace turboq::logger;

BackendThread::setSink(std::make_shared<MySink>()); // a turboq::logger::Sink

TURBOQ_LOGGER_LOG_CALL(LogLevel::Info, "order {} filled at {}", orderID, price);

BackendThread::flush(); // wait until this thread's messages are written and the sink flushed
BackendThread::stop();  // optional: done at exit
```

- Each thread logs to its own bounded queue. When it is full, the message is dropped and counted;
  the count is printed to stderr when the queue is destroyed.
- An exception thrown by the sink skips that one message: it is reported on stderr (at most once
  a second) and the backend carries on.

### License

Distributed under the MIT License. See LICENSE for details.
