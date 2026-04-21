# Build and Flash

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| [PlatformIO Core](https://platformio.org/install/cli) | ≥ 6 | CLI or VS Code extension |
| Python | ≥ 3.8 | Required by PlatformIO |
| ESP32 Arduino platform | Installed automatically | `espressif32` |
| Google Test | Installed automatically | For the native test target |

---

## Repository Structure

```
SolarChargeController/
├── src/
│   └── main.cpp                  # Arduino entry point (setup / loop)
├── lib/                          # Project-specific libraries
│   ├── Battery/                  # BatteryManager + BatteryProfileSelector
│   ├── ChargeController/         # Main orchestrator
│   ├── MpptController/           # P&O MPPT algorithm
│   ├── Actuators/                # DPSxDcConverter, PwmDcConverter
│   ├── Sensors/                  # SensorINA226, SensorI2C
│   ├── Measurements/             # MeasurementsIf, DPSxMeasurements
│   ├── Initializer/              # Hardware singleton (DPS / PWM)
│   ├── Mqtt/                     # MqttClient, MqttSolarControllerBridge
│   ├── OtaHandler/               # ArduinoOTA wrapper
│   ├── Logger/                   # Portable ESP_LOG* shim
│   ├── Config.h                  # Build-time constants and battery profiles
│   └── Utility.h                 # Timer, Measurements struct, helpers
├── include/
│   ├── secrets.h.template        # Copy → secrets.h and fill in credentials
│   └── README                    # Note about secrets.h
├── test/                         # Unit test sources + mocks
├── docs/                         # Documentation
└── platformio.ini                # PlatformIO project configuration
```

---

## Secrets File

Before building, create `include/secrets.h` from the template:

```bash
cp include/secrets.h.template include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#define MQTT_BROKER    "192.168.1.10"       // IP or hostname of your MQTT broker
#define MQTT_USERNAME  "homeassistant"
#define MQTT_PASSWORD  "yourpassword"
#define WIFI_SSID      "YourWiFiSSID"
#define WIFI_PASSWORD  "YourWiFiPassword"
#define OTA_PASSWORD   "yourOtaPassword"
```

> `secrets.h` is listed in `.gitignore` and must **never** be committed.

---

## Build Environments

Two PlatformIO environments are defined in `platformio.ini`:

| Environment | Target | Purpose |
|---|---|---|
| `esp32dev` | ESP32 hardware | Production firmware |
| `esp32dev-test` | Native (Linux/macOS/Windows) | Unit tests (no hardware required) |

---

## Building for ESP32

### Default (DPS5005 + MQTT)

```bash
pio run -e esp32dev
```

Both `-D DPS_DC_CONVERTER` and `-D MQTT_CLIENT` are enabled by default in `platformio.ini`.

### PWM + INA226 variant (no MQTT)

In `platformio.ini`, remove `-D DPS_DC_CONVERTER` and `-D MQTT_CLIENT` from `build_flags`:

```ini
[env:esp32dev]
build_flags =
    -std=gnu++17
    -Ilib
    ...
    ; -D DPS_DC_CONVERTER   ← remove this line
    ; -D MQTT_CLIENT        ← remove this line if WiFi is not needed
```

Then rebuild:

```bash
pio run -e esp32dev
```

---

## Flashing

Connect the ESP32 via USB, then:

```bash
pio run -e esp32dev -t upload
```

PlatformIO auto-detects the serial port. To specify manually:

```bash
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

---

## Serial Monitor

```bash
pio device monitor --baud 115200
```

Or combined build + upload + monitor:

```bash
pio run -e esp32dev -t upload && pio device monitor --baud 115200
```

### Sample Boot Output

```
[INFO]  [00:00:00.012] Main: setup() start
[INFO]  [00:00:00.013] Main: Initialising MQTT client...
[INFO]  [00:00:01.250] MqttClient: WiFi connected. IP: 192.168.1.50
[INFO]  [00:00:01.251] Main: mqttClient.init() done
[INFO]  [00:00:01.252] Main: bridge.init() done
[INFO]  [00:00:01.280] BatteryProfileSelector: NVS profile loaded: type=0 maxV=12600mV rechargeV=12400mV maxI=10000mA
[INFO]  [00:00:01.300] Main: hardware.init() done
[INFO]  [00:00:01.305] Main: controller.init() done
[INFO]  [00:00:01.310] Main: otaHandler.init() done
[INFO]  [00:00:01.311] Main: setup() complete
```

---

## Log Level Control

All levels are enabled at runtime by default (`esp_log_level_set("*", ESP_LOG_DEBUG)` in `setup()`).

To quiet a specific noisy module at runtime, call from your own code or a temporary test:

```cpp
esp_log_level_set("DPSxDcConverter", ESP_LOG_WARN);  // suppress INFO/DEBUG for DPS
esp_log_level_set("*", ESP_LOG_ERROR);               // silence everything except errors
```

---

## Running Unit Tests

The test target compiles and runs on the host machine — no ESP32 required.

```bash
pio test -e esp32dev-test
```

Expected output:

```
[==========] Running X tests from Y test suites.
[  PASSED  ] X tests.
```

See [Testing](Testing.md) for details on the test structure and how to add new tests.

---

## Build Flags Reference

See [Configuration](Configuration.md) for a full reference of all build flags and their effects.
