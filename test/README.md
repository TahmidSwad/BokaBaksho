Boka_Baksho Test Suite
======================

Status: **placeholder — no tests exist yet.**

Testing Strategy
----------------

The intended approach is PlatformIO's native test environment for host-side
unit testing, using the Unity test framework. Tests would run on the
development machine (not on the ESP32).

The components considered worth host-side coverage are:

- EventBus: subscription, posting, and dispatching
- AppManager: registration, navigation stack, input routing

Hardware-dependent code (drivers, services, `DisplayService`, `BleService`)
is not host-testable without mocking the Arduino HAL and is instead validated
on-device over the serial monitor.

Current reality (2026-10-03)
----------------------------

- This directory contains only `README.md` — there are no test source files.
- `platformio.ini` defines a single environment, `esp32doit-devkit-v1`.
  **There is no `[env:native]`**, so `pio test -e native` has nothing to build
  against.

Until those two gaps are closed, `pio test` does not exercise anything. See
`docs/build-and-test.md` §5 for the steps required to make the suite real.

Running Tests (once implemented)
--------------------------------

Compile and run tests:

    pio test

Run tests for a specific environment:

    pio test -e native

Run tests with verbose output:

    pio test -v

Adding New Tests
----------------

1. Add a native environment to `platformio.ini`:

       [env:native]
       platform = native
       test_framework = unity
       build_flags = -Isrc

2. Create a test file under `test/`, e.g. `test/test_core/test_core.cpp`.

3. Include the Unity framework:

       #include <unity.h>

4. Include only the component under test:

       #include "core/event_bus.h"

5. Write test functions using `TEST_ASSERT_*` macros and a `main()` that
   runs them:

       int main() {
           UNITY_BEGIN();
           RUN_TEST(test_something);
           return UNITY_END();
       }

Notes
-----

- All tests must use static allocation; no dynamic memory is allowed anywhere
  in this project.
- `core/` headers include `<Arduino.h>`, so a host build needs either a shim
  header or `ARDUINO`-guarded includes before `pio test -e native` will
  compile.
- Do not include `src/main.cpp` from a test — it drags in the entire firmware
  and all of its globals.
- Tests that interact with hardware (OLED, buttons, BLE) require the ESP32
  target and are run via the serial monitor using the debug keys
  `l r e b + -` rather than as host-side unit tests.
