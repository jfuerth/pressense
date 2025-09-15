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

## Referencing library code from tests

PlatformIO automatically resolves includes from `$projectRoot/lib/*` so if you have a header
`$projectRoot/lib/foobar/foo.h` then you can include it in a test with `#include <foo.h>`.
PlatformIO will then also link the foobar library to the test with no further configuration.

## Reference materials

More information about PlatformIO Unit Testing:
- https://docs.platformio.org/en/latest/advanced/unit-testing/index.html

The classic example of this hybrid testing setup in PlatformIO:
- https://github.com/platformio/platformio-examples/tree/develop/unit-testing/calculator
