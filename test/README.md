The tests are written with the Unity unit testing framework for C/C++, and are executed under the control of
PlatformIO's `pio` command.

## Language

This project uses the C++17 standard, as (I think) is required by the Arduino+ESP-IDF libraries.

## Organization

Test cases are broken down into three directories:

* test_common/ -- tests that run on the embedded hardware and on the Linux/MacOS/CI environment (PlatformIO "Native" platform)
* test_desktop/ -- tests that only run on the dev environment
* test_embedded/ -- tests that only run on the esp32 hardware

When adding new categories in the future, note that PlatformIO requires all directories containing test suites to be named `test_*`. 

## Running the tests

To run the common + native tests:

```
source ~/.platformio/penv/bin/activate
pio test -e native
```

To run the common + esp32 tests (requires an ESP 32 connected via USB/serial):

```
source ~/.platformio/penv/bin/activate
pio test -e esp32dev
```

Especially When running the native tests, just run the whole suite. It only takes a second or two
and it's faster than running one group of tests at a time.

## Testing Real-Time Memory Safety

For real-time audio applications, it's critical to ensure no heap allocations occur during audio processing. This project includes a custom memory tracking utility to verify this.

### Memory Tracking Usage

The `memory_tracker.hpp` utility allows you to verify that code blocks don't perform heap allocations:

```cpp
#define ENABLE_MEMORY_TRACKING
#include "memory_tracker.hpp"

void test_realtime_code() {
    // Setup code (can allocate)
    auto allocator = createAllocator();
    
    // Test real-time critical section
    TEST_NO_HEAP_ALLOCATIONS({
        // This code must not call new/delete/malloc/free
        auto& voice = allocator.voiceFor(60);
        voice.trigger(440.0f, 0.8f);
        voice.release();
    });
}
```

### What It Catches

This memory tracking system is particularly valuable for catching **subtle heap allocations** that are easy to miss:

1. **STL Container Growth**: `std::vector::push_back()`, `std::string` operations
2. **Map/Set Operations**: `std::map[key]`, `std::unordered_map::insert()`
3. **Smart Pointer Operations**: `std::make_shared()`, `std::make_unique()`
4. **Hidden Library Allocations**: Third-party libraries that allocate internally
5. **Exception Handling**: Some exception mechanisms can allocate


### Alternative Approaches

1. **ScopedMemoryTracker**: For more complex tracking scenarios
2. **Manual tracking**: Call `MemoryTracker::startTracking()` and `MemoryTracker::stopTracking()` directly
3. **Integration with existing allocators**: Can be extended to track specific allocator usage

### Limitations

- Only tracks global `new`/`delete` operators
- Requires `ENABLE_MEMORY_TRACKING` to be defined before including the header
- Does not track stack allocations or static memory usage
- Not suitable for production builds (testing only)

## Referencing library code from tests

PlatformIO automatically resolves includes from `$projectRoot/lib/*` so if you have a header
`$projectRoot/lib/foobar/foo.h` then you can include it in a test with `#include <foo.h>`.
PlatformIO will then also link the foobar library to the test with no further configuration.

## Reference materials

More information about PlatformIO Unit Testing:
- https://docs.platformio.org/en/latest/advanced/unit-testing/index.html

The classic example of this hybrid testing setup in PlatformIO:
- https://github.com/platformio/platformio-examples/tree/develop/unit-testing/calculator
