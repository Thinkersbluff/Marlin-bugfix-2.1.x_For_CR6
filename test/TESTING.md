# Marlin Unit Tests

This directory contains native unit tests for Marlin firmware components that can be compiled and run on a host machine (Linux, macOS, or Windows with appropriate toolchain).

## Test Files

### test_queue.cpp
Tests for the G-code command queue ring buffer (`Marlin/src/gcode/queue.h`):
- Basic enqueue/dequeue operations
- FIFO ordering
- Ring buffer wraparound behavior
- Full and empty state detection
- Edge cases (empty queue operations, special characters)

**Tested components:**
- `GCodeQueue::RingBuffer::enqueue()`
- `GCodeQueue::RingBuffer::advance_r()`
- `GCodeQueue::RingBuffer::advance_w()`
- `GCodeQueue::RingBuffer::clear()`
- `GCodeQueue::RingBuffer::full()`
- `GCodeQueue::RingBuffer::empty()`

### test_m1125.cpp
Tests for M1125 pause/resume command filtering logic:
- Detection and filtering of pause-triggering commands (M600, M1125)
- Preservation of normal G-code commands
- Case-insensitive matching
- Handling of parameters, comments, and whitespace
- Edge cases (empty strings, null pointers, substring confusion)

**Tested logic:**
- `m1125_should_skip_saved_command()` - prevents saved M600/M1125 from being replayed after resume
- `m1125_command_matches()` - case-insensitive command name matching

**Context:** The M1125 pause handler preserves commands from the SD ring buffer during pause and restores them on resume. These tests verify that pause-triggering commands are correctly filtered to prevent infinite pause loops.

## Running Tests

### Local Execution (Linux/macOS or Windows with GCC toolchain)

Run all tests:
```bash
make unit-test-all-local
```

Or use PlatformIO directly:
```bash
pio test -e linux_native_test
```

Run a specific test:
```bash
pio test -e linux_native_test -f test_queue
pio test -e linux_native_test -f test_m1125
```

### CI Execution

Tests run automatically via GitHub Actions on pull requests and pushes to `bugfix-2.1.x` branch.

See `.github/workflows/ci-unit-tests.yml` for CI configuration.

### Windows Requirements

Native tests require GCC. Options:
1. **WSL (Windows Subsystem for Linux)** - recommended
2. **MSYS2** with MinGW-w64:
   ```bash
   pacman -S mingw-w64-x86_64-toolchain
   ```
   Add `C:\msys64\mingw64\bin` to PATH
3. **Docker** - use the included Dockerfile:
   ```bash
   make unit-test-all-local-docker
   ```

## Test Framework

Tests use the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework (v2.5.2+).

### Writing New Tests

1. Create a new file in `test/` directory: `test_<component>.cpp`

2. Include the test framework and component headers:
```cpp
#include "../test/unit_tests.h"
#include "src/<path>/<header>.h"
```

3. Write tests using the `MARLIN_TEST` macro:
```cpp
MARLIN_TEST(suite_name, test_name) {
  // Setup
  int result = function_under_test(42);
  
  // Assertions
  TEST_ASSERT_EQUAL(42, result);
  TEST_ASSERT_TRUE(condition);
  TEST_ASSERT_FALSE(other_condition);
  TEST_ASSERT_EQUAL_STRING("expected", actual_string);
}
```

4. Run tests to verify:
```bash
pio test -e linux_native_test -f test_<component>
```

### Common Unity Assertions

- `TEST_ASSERT_TRUE(condition)`
- `TEST_ASSERT_FALSE(condition)`
- `TEST_ASSERT_EQUAL(expected, actual)`
- `TEST_ASSERT_EQUAL_STRING(expected, actual)`
- `TEST_ASSERT_NULL(pointer)`
- `TEST_ASSERT_NOT_NULL(pointer)`
- `TEST_ASSERT_EQUAL_FLOAT(expected, actual, tolerance)`

See [Unity documentation](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md) for complete reference.

## Test Stubs

`test_stubs.cpp` provides minimal stub implementations for hardware-dependent symbols needed by tests but not directly tested. Stubs are only compiled for `linux_native_test` environment.

Add new stubs here when tests require hardware-dependent symbols (UI, SD card, thermal management, etc.).

## Coverage Targets

Priority areas for test coverage:
- [ ] G-code parsing and command handling
- [x] Queue/ring buffer operations
- [x] M1125 pause/resume filtering
- [ ] Host notification caching and suppression
- [ ] M73 progress reporting
- [ ] Temperature management logic
- [ ] Motion planning helpers
- [ ] SD card command processing

## Troubleshooting

**"CC is not recognized" error on Windows:**
- Install MSYS2/MinGW or use Docker/WSL
- Or skip local testing and rely on CI

**Tests fail to compile:**
- Check for missing stubs in `test_stubs.cpp`
- Verify includes are correct in test files
- Use `-vvv` flag for verbose output: `pio test -e linux_native_test -vvv`

**Tests compile but fail:**
- Check assertion messages for details
- Add debug output with `SERIAL_ECHOLNPGM()` (may not work in native tests)
- Use Unity's `TEST_MESSAGE()` for debugging
