# Testing

The project uses **Google Test** on a native (desktop) build target — no ESP32 hardware is needed to run the tests.

---

## Running Tests

```bash
pio test -e esp32dev-test
```

All test sources are compiled with the native platform toolchain. The result is a binary that runs directly on your PC/Mac/Linux machine.

Expected output:

```
[==========] Running 61 tests from 5 test suites.
[----------] Global test environment set-up.
...
[  PASSED  ] 61 tests.
```

---

## Test Environment (`esp32dev-test`)

Defined in `platformio.ini`:

```ini
[env:esp32dev-test]
platform = native
test_framework = googletest
build_flags =
    -std=gnu++17
    -Ilib
    -Ilib/Battery
    -Ilib/ChargeController
    -Ilib/MpptController
    -Ilib/Actuators
    -Ilib/Logger
    -Ilib/Measurements
    -Ilib/Sensors
    -Itest
    -std=c++17
test_ignore = src/*
build_src_filter = +<lib/Battery/> +<lib/ChargeController/> +<lib/MpptController/> +<lib/Actuators/>
```

The `src/main.cpp` is excluded (`test_ignore = src/*`). Only the library source files under test are compiled.

---

## Test Files

| File | Component under test | Key scenarios |
|---|---|---|
| `test/test_battery_manager.cpp` | `BatteryManager` | All mode transitions, Fault terminal state, CV→Done, Done→CC recharge, precharge current cap (500 mA), charging-disabled timeout (60 s) |
| `test/test_battery_profile_selector.cpp` | `BatteryProfileSelector` | NVS load/save, type switching, per-field validation, ±10% margin, observer callback |
| `test/test_charge_controller.cpp` | `ChargeController` | PV power unavailability, stale measurement handling, MPPT gating, PI clamping |
| `test/test_mppt_controller.cpp` | `MpptController` | Direction tracking, variable step size, control clamping [0–100] |
| `test/test_utility.cpp` | `parseIntSafe`, `Timer` | Safe integer parsing edge cases, timer active flag, duration tracking |

---

## Mock Infrastructure

Because the tests run on a desktop without ESP32 peripherals, the hardware interfaces are replaced with mock implementations.

### `MockMeasurements` (`test/MockMeasurements.h`)

Implements `MeasurementsIf`. Provides direct setters:

```cpp
MockMeasurements pvMeasurements;
pvMeasurements.setVoltage_mV(18000);
pvMeasurements.setCurrent_mA(5000);
pvMeasurements.setValid(true);
pvMeasurements.setLastTimeUpdated(0);
```

### `MockActuator` (`test/MockActuator.h`)

Implements `ActuatorIf`. Records the last call to `applyControl()`:

```cpp
MockActuator actuator;
// After controller.update():
int lastControl = actuator.getLastControl();
```

### Arduino Shim (`test/Arduino_impl.cpp`, `test/Arduino.h`)

Provides stub implementations of `millis()`, `constrain()`, `Wire`, `Serial2`, and other Arduino/ESP32 APIs so the library code compiles without the real Arduino framework.

---

## Time Control in Tests

Time-dependent tests (timer timeouts, stale measurement windows) use two helpers defined in `Arduino_impl.cpp`:

```cpp
void advance_millis(unsigned long ms); // advance the fake clock by ms milliseconds
void reset_millis();                   // reset the fake clock to 0
```

Example — testing the 60-second charging-disabled timeout:

```cpp
batteryManager.update(measurements, false); // chargingAvailable = false → timer starts
advance_millis(60001);                      // advance past the 60 s threshold
batteryManager.update(measurements, false);
EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Idle);
```

---

## NVS Mock (`test/nvs.h`, `test/nvs_flash.h`)

The ESP-IDF NVS functions are stubbed to use an in-memory `std::map` for `BatteryProfileSelector` tests. This allows testing the full NVS load/save logic without flash hardware.

---

## Adding a New Test

1. Create (or add to) a file in `test/` following the pattern `test_*.cpp`.
2. Include `<gtest/gtest.h>`.
3. Use `TEST_F` with an appropriate fixture or `TEST` for standalone cases.
4. Run `pio test -e esp32dev-test` to verify.

Example:

```cpp
#include <gtest/gtest.h>
#include "MpptController.h"

TEST(MpptControllerTest, InitialisesToZero)
{
    MpptController mppt;
    mppt.init();
    EXPECT_EQ(mppt.getRequestedOutput(), 0);
}
```

---

## Logger Behaviour in Tests

`Logger.h` detects the native target and suppresses all output except `ESP_LOGE` (which goes to `stderr`). This keeps test output clean while still surfacing error-level messages during debugging.

To temporarily enable verbose logging during a specific test run, edit `lib/Logger/Logger.h` and map `ESP_LOGI`/`ESP_LOGD` to `fprintf(stdout, ...)` in the `#else` block.
