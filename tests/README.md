# nlmon Testing Infrastructure

This directory contains the comprehensive testing infrastructure for nlmon, including unit tests, integration tests, performance benchmarks, and memory testing tools.

## Directory Structure

```
tests/
├── unit/                   # Unit tests
│   ├── test_framework.h    # Minimal testing framework
│   ├── test_ring_buffer.c  # Ring buffer tests
│   ├── test_filter.c       # Filter system tests
│   └── test_object_pool.c  # Object pool tests
├── integration/            # Integration tests
│   ├── netlink_simulator.h # Netlink event simulator
│   ├── netlink_simulator.c
│   ├── test_e2e_processing.c      # End-to-end tests
│   ├── test_plugin_loading.c     # Plugin system tests
│   └── test_config_loading.c     # Configuration tests
├── benchmarks/             # Performance benchmarks
│   ├── benchmark_framework.h      # Benchmarking framework
│   ├── bench_event_processing.c  # Event processing benchmarks
│   ├── bench_filter_evaluation.c # Filter evaluation benchmarks
│   └── bench_memory_usage.c      # Memory usage benchmarks
├── memory/                 # Memory testing
│   ├── valgrind_test.sh    # Valgrind leak detection
│   ├── valgrind.supp       # Valgrind suppressions
│   ├── test_stability.c    # Long-running stability test
│   └── profile_memory.sh   # Memory profiling script
└── README.md               # This file
```

## Building Tests

### Build All Tests
```bash
make unit-tests
make integration-tests
make benchmarks
make memory-tests
```

### Build Specific Test Suites
```bash
# Unit tests only
make test_unit_ring_buffer
make test_unit_filter
make test_unit_object_pool

# Integration tests only
make test_integration_e2e_processing
make test_integration_plugin_loading
make test_integration_config_loading

# Benchmarks only
make bench_event_processing
make bench_filter_evaluation
make bench_memory_usage

# Memory tests only
make test_stability
```

## Running Tests

### Unit Tests
Run all unit tests:
```bash
make run-unit-tests
```

Run individual unit test:
```bash
./test_unit_ring_buffer
./test_unit_filter
./test_unit_object_pool
```

### Integration Tests
Run all integration tests:
```bash
./test_integration_e2e_processing
./test_integration_plugin_loading
./test_integration_config_loading
```

### Performance Benchmarks
Run all benchmarks:
```bash
make run-benchmarks
```

Run individual benchmark:
```bash
./bench_event_processing
./bench_filter_evaluation
./bench_memory_usage
```

### Memory Testing

#### Valgrind Leak Detection
Run valgrind on all tests:
```bash
make run-valgrind
```

Run valgrind on specific test:
```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_unit_ring_buffer
```

#### Stability Test
Run long-running stability test:
```bash
make run-stability          # 60 seconds
./test_stability 300        # 5 minutes
./test_stability 3600       # 1 hour
```

#### Memory Profiling
Profile memory usage with massif:
```bash
make profile-memory
./tests/memory/profile_memory.sh test_stability 60
```

View profiling results:
```bash
ms_print massif.out.<pid>
```

## Test Framework

### Unit Test Framework
The unit test framework (`tests/unit/test_framework.h`) provides:
- Simple test definition with `TEST(name)` macro
- Assertion macros: `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_EQ`, `ASSERT_NULL`, etc.
- Test suite management with `TEST_SUITE_BEGIN` and `TEST_SUITE_END`
- Colored output for better readability
- Automatic test statistics

Example:
```c
#include "test_framework.h"

TEST(my_test)
{
    int value = 42;
    ASSERT_EQ(value, 42);
    ASSERT_TRUE(value > 0);
}

TEST_SUITE_BEGIN("My Test Suite")
    RUN_TEST(my_test);
TEST_SUITE_END()
```

### Benchmark Framework
The benchmark framework (`tests/benchmarks/benchmark_framework.h`) provides:
- Latency benchmarks with `BENCHMARK(name, iterations)`
- Throughput benchmarks with `THROUGHPUT_BENCHMARK(name, duration)`
- Memory benchmarks with `MEMORY_BENCHMARK(name)`
- Automatic statistics calculation (avg, min, max, ops/sec)

Example:
```c
#include "benchmark_framework.h"

BENCHMARK(my_operation, 10000)
{
    // Code to benchmark
    my_function();
}

THROUGHPUT_BENCHMARK(my_throughput, 5.0)
{
    my_function();
    return 1; // Return number of operations
}

BENCHMARK_SUITE_BEGIN("My Benchmarks")
    RUN_BENCHMARK(my_operation);
    RUN_THROUGHPUT_BENCHMARK(my_throughput);
BENCHMARK_SUITE_END()
```

## CI/CD Integration

The project includes a GitHub Actions workflow (`.github/workflows/ci.yml`) that:
- Builds nlmon with all features
- Runs unit tests
- Runs integration tests
- Runs benchmarks
- Performs memory leak detection with valgrind
- Runs stability tests

## Test Coverage

### Unit Tests
- Ring buffer operations (push, pop, wrap-around, concurrent access)
- Filter parsing, compilation, and evaluation
- Object pool allocation and reuse
- Thread pool management
- Rate limiting
- Event processing

### Integration Tests
- End-to-end event processing pipeline
- Filter integration with event processor
- Storage layer integration
- Plugin loading and lifecycle
- Configuration loading and hot-reload

### Performance Benchmarks
- Event processing throughput (ops/sec)
- Filter evaluation performance
- Memory allocation vs object pool
- Ring buffer operations
- Latency measurements

### Memory Tests
- Memory leak detection with valgrind
- Long-running stability tests
- Memory profiling with massif
- Resource usage tracking

## Requirements

### Build Requirements
- GCC or Clang
- Make
- pkg-config
- libnl-3-dev
- libnl-route-3-dev
- libev-dev
- libncursesw5-dev
- libyaml-dev (optional, for config tests)
- libsqlite3-dev (optional, for storage tests)
- libmicrohttpd-dev (optional, for web tests)

### Test Requirements
- valgrind (for memory testing)
- valgrind-dbg (optional, for ms_print)

Install on Ubuntu/Debian:
```bash
sudo apt-get install \
    libnl-3-dev \
    libnl-route-3-dev \
    libev-dev \
    libncursesw5-dev \
    libyaml-dev \
    libsqlite3-dev \
    libssl-dev \
    libmicrohttpd-dev \
    valgrind \
    valgrind-dbg \
    pkg-config
```

## Best Practices

1. **Run tests before committing**: Always run `make run-unit-tests` before committing changes
2. **Check for memory leaks**: Run `make run-valgrind` periodically
3. **Benchmark performance**: Run benchmarks after performance-related changes
4. **Test stability**: Run stability tests for at least 5 minutes after major changes
5. **Update tests**: Add tests for new features and bug fixes

## Troubleshooting

### Tests fail to build
- Check that all dependencies are installed: `make check-deps`
- Ensure you've built the main project first: `make all`

### Valgrind reports false positives
- Add suppressions to `tests/memory/valgrind.supp`
- Common false positives: ncurses, pthread, libnl, OpenSSL

### Benchmarks show poor performance
- Ensure system is not under load
- Disable CPU frequency scaling
- Run benchmarks multiple times for consistency

### Stability test crashes
- Check system resources (memory, file descriptors)
- Review logs for error messages
- Run with valgrind to detect memory issues

## Contributing

When adding new features:
1. Write unit tests for core functionality
2. Add integration tests for component interactions
3. Create benchmarks for performance-critical code
4. Test for memory leaks with valgrind
5. Update this README if adding new test types
