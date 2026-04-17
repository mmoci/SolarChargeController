
# Solar Charge Controller - Library Components

This directory contains the project-specific libraries for an **ESP32-based MPPT (Maximum Power Point Tracking) Solar Charge Controller**. The modular architecture enables flexible support for different sensor and actuator configurations.

## Project Overview

The Solar Charge Controller is a sophisticated embedded system designed to optimize energy harvesting from solar panels and manage battery charging. It implements:

- **MPPT Algorithm**: Continuously tracks and extracts maximum power point from solar panels
- **Multi-Stage Battery Charging**: Idle → Precharge → CC (Constant Current) → CV (Constant Voltage) → Done
- **Flexible Hardware Architecture**: Supports multiple sensor and actuator implementations
- **Safety Management**: Fault detection and handling
- **Real-time Control**: 3ms update loop with PI (Proportional & Integral) control
- **Persistent Battery Profiles**: NVS-backed profile storage with runtime type switching and per-field overrides
- **MQTT Telemetry & Remote Control**: Full HomeAssistant integration — real-time telemetry, availability, and remote battery profile management

## Platform & Configuration

- **Target Board**: ESP32-DevKit
- **Framework**: Arduino
- **Language Standard**: C++17
- **Default Actuator**: DPS DC-DC Converter (with PWM fallback)
- **Communication**: I2C for sensors, Serial2/Modbus RTU for DPS, Serial (115200 baud) for debugging
- **Remote Control**: MQTT over WiFi (optional, enabled with `-D MQTT_CLIENT`)

## Library Structure

### Core Components

#### **ChargeController** (`ChargeController/`)
Main orchestration component that coordinates all charging operations:
- Monitors PV (photovoltaic) and battery measurements
- Manages battery charging state machine
- Implements MPPT control algorithm
- Applies voltage and current limiting (PI control)
- Soft ramp control to prevent sudden duty cycle changes
- Determines charging availability and handles power unavailability timeouts

#### **MpptController** (`MpptController/`)
Pure algorithm implementation for Maximum Power Point Tracking:
- Perturb & Observe (P&O) algorithm variant
- Adjustable step size control
- 0-100% duty cycle range
- Stateless design for maximum efficiency

#### **BatteryManager** (`Battery/`)
Intelligent battery charge controller with state management:
- **Charging Modes**:
  - `Idle`: Charging not allowed
  - `Precharge`: Very low current charging for battery recovery
  - `CC`: Constant Current charging phase
  - `CV`: Constant Voltage phase (voltage clamping active)
  - `Done`: Battery fully charged
  - `Fault`: Safety fault condition detected
- Battery profile-based limits (chemistry-aware)
- SOC (State of Charge) and battery state tracking
- Automatic mode transitions based on voltage/current thresholds

#### **BatteryProfileSelector** (`Battery/`)
NVS-backed battery profile management with runtime switching:
- Predefined profiles: `LIION_3S`, `LIION_4S`, `LIFEPO4_4S`, `CUSTOM`
- **Type + Overrides pattern**: selecting a type loads its defaults; individual fields (max voltage, recharge voltage, precharge voltage, load disconnect voltage, max charging current) can be overridden independently and persisted
- Profiles survive power cycles via ESP-IDF NVS flash storage
- Validated on load and on every field write; invalid values are rejected with typed `Result` codes
- Initialises at startup: loads from NVS on first boot, falls back to compiled-in defaults if NVS is empty or corrupt

### Hardware Interface Layers

#### **Sensors** (`Sensors/`)
Abstracts voltage and current measurement:
- `SensorINA226`: Dual-channel INA226 current/voltage sensors for precise measurements
  - PV voltage and current monitoring
  - Battery voltage and current monitoring
  - I2C-based communication
- `DPSxMeasurements`: Alternative interface for DPS DC-converter integrated measurements

#### **Actuators** (`Actuators/`)
PWM/DC-DC conversion control layer:
- `PwmDcConverter`: PWM-based DC-DC control (0-100% duty cycle)
- `DPSxDcConverter`: DPS serial protocol-based DC-DC converter control
- Abstracted through `ActuatorIf` interface for flexibility
- Supports device initialization and runtime updates

#### **MQTT** (`Mqtt/`)
Optional remote monitoring and control layer, enabled with `-D MQTT_CLIENT`:

- `MqttClient`: Thin transport layer over PubSubClient
  - Blocking WiFi connect on startup, automatic MQTT reconnection with backoff
  - Per-topic subscription callbacks (`subscribe(topic, callback)`)
  - Multiple `onConnect` callbacks — each component registers independently without overwriting others
  - Availability state: Last Will publishes `offline` on unexpected disconnect; `online` published on every successful connect
  - Retained publish support for state topics

- `MqttSolarControllerTopicBuilder`: Generates all topic strings from a device ID, avoiding hardcoded strings and enabling multiple devices on the same broker

- `MqttSolarControllerBridge`: Domain bridge between MQTT and the charge controller
  - **Telemetry** (published every 5 s): PV voltage/current/power, battery voltage/current, charging mode, control signal %
  - **Battery profile commands**: subscribes all six `/set` topics; validates, persists to NVS, and restarts the device on change
  - **Profile state** (retained): republishes all six profile fields on every (re)connect so HomeAssistant always reflects current values
  - **HomeAssistant MQTT Discovery**: auto-registers 7 sensor, 1 select, and 5 number entities under a single HA device; republished on every reconnect so HA picks them up after a restart

Topic layout:
```
solar/{deviceId}/availability
solar/{deviceId}/telemetry/{measurement}
solar/{deviceId}/profile/{field}/state
solar/{deviceId}/profile/{field}/set
homeassistant/{component}/{deviceId}/{objectId}/config
```

### Supporting Components

#### **Measurements Interface** (`Measurements/`)
- `MeasurementsIf`: Base interface for sensor abstraction
- Voltage and current data structures
- Stale-measurement detection (`isMeasurementValid()` / `lastTimeUpdated()`)

#### **Logger** (`Logger/`)
Portable logging shim that routes log calls to the appropriate backend:
- **ESP32**: delegates to `esp_log.h` macros (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGD`) — full log-level filtering via `esp_log_level_set`
- **Arduino (non-ESP32)**: maps to `Serial.printf` with level prefix
- **Native test target**: only errors go to `stderr`; all other levels are suppressed (keeps test output clean)
- Enables the same `ESP_LOG*` call sites in all `.cpp` files regardless of build target

#### **Configuration** (`Config.h`)
Build-time and runtime configuration constants:
- INA226 I2C addresses and shunt resistor values (with `PV_SHUNT_mOhm` / `BATTERY_SHUNT_mOhm`)
- Charge controller PI gains and soft-ramp step sizes (tuned separately for DPS/PWM via `#ifdef`)
- `STALE_LOG_INTERVAL` (5 s): minimum interval between repeated stale-measurement warnings, preventing serial flood during sensor loss
- Predefined battery profiles (`LI_ION_3S_DEFAULT`, `LI_ION_4S_DEFAULT`, `LIFEPO4_4S_DEFAULT`, `CUSTOM_DEFAULT`)

#### **Utilities** (`Utility.h`)
- `Timer`: elapsed-time helper with explicit `m_active` flag (safe when `millis()` returns 0)
- `Measurements`: simple voltage/current value struct
- `parseIntSafe()`: null-safe integer parsing using `std::strtol` (avoids `std::stol` which throws — incompatible with ESP32's `-fno-exceptions`)

## Build Targets

### Production Target: `esp32dev`
- Compiles for ESP32 hardware
- Full MPPT charging controller implementation
- Default uses DPS DC-Converter (`-D DPS_DC_CONVERTER` flag)
- MQTT support enabled with `-D MQTT_CLIENT`
- Optimized for C++17

### Test Target: `esp32dev-test`
- Native platform (desktop testing)
- Google Test framework integration
- Isolated component unit testing without hardware
- Battery manager, charge controller, MPPT algorithm, and utility tests included

## Architecture Highlights

### Design Patterns

1. **Dependency Injection**: Core components receive sensor and actuator dependencies
2. **Interface Abstraction**: Hardware implementations behind `MeasurementsIf` and `ActuatorIf`
3. **Composition Pattern**: ChargeController composes Battery, MPPT, and measurements
4. **Compile-time Configuration**: Build flags enable/disable hardware variants (`DPS_DC_CONVERTER`, `MQTT_CLIENT`)
5. **Type + Overrides**: BatteryProfileSelector loads a full type default then stores only field overrides in NVS, keeping storage minimal
6. **Layered MQTT**: Transport (MqttClient) is separated from domain logic (MqttSolarControllerBridge) — the bridge has no knowledge of WiFi or reconnection

### Data Flow

```
Solar Panel → PV Sensors → ChargeController → MPPT Algorithm → PWM/DPS Actuator → Battery
                                    ↓
                           BatteryManager → Battery Sensors
                                    ↓
                          Battery State Machine
                                    ↓
                        BatteryProfileSelector (NVS)
                                    ↓
              MqttSolarControllerBridge → MqttClient → MQTT Broker → HomeAssistant
```

### Initialization & Loop Order

**`setup()`** (order is significant):
1. `Serial.begin()` / `Wire.begin()` — peripherals first
2. `mqttClient.init()` — connects WiFi (blocking); configures broker but does **not** connect MQTT yet
3. `profileSelector.init()` — loads NVS profile; must precede bridge and controller init
4. `bridge.init()` — registers `onConnect` callback and command subscriptions on mqttClient
5. `Serial2.begin()` / hardware actuator init — DPS or PWM
6. `controller.init(profileSelector.getCurrentProfile())` — applies the loaded profile

**`loop()`** order:
1. `mqttClient.process()` — drives reconnection and incoming message dispatch
2. Sensor / actuator `update()` — fresh measurements before control
3. `controller.update()` — MPPT + battery state machine
4. Telemetry timer — publish fresh values every 5 s via `bridge.publishTelemetry()`

## Testing

The project includes comprehensive unit tests (**61 test cases**, all passing):
- `test_battery_manager.cpp`: Battery state machine and charging mode transitions — including Fault terminal state, CV→Done, Done→CC recharge, precharge current cap (500 mA), and charging-disabled timeout (60 s)
- `test_charge_controller.cpp`: Main controller logic and MPPT integration — including time-based PV power unavailability, stale-measurement handling, and MPPT gate (only perturbs on fresh measurements)
- `test_mppt_controller.cpp`: MPPT algorithm verification
- `test_utility.cpp`: Helper function validation

Mock implementations provided for testing without hardware. Time-dependent tests use `advance_millis()` / `reset_millis()` helpers in `Arduino_impl.cpp` to control `millis()` deterministically.

## Current Status & Future Improvements

### Features Implemented
✓ Modular sensor/actuator architecture
✓ Multi-stage battery charging (Idle/Precharge/CC/CV/Done/Fault)
✓ MPPT algorithm implementation (Perturb & Observe)
✓ PI control for voltage/current limiting
✓ Dual hardware support (PWM & DPS5005 via Modbus RTU)
✓ Unit testing framework (Google Test, native platform) — 61 tests
✓ NVS-backed battery profile selection with runtime field overrides (`BatteryProfileSelector`)
✓ MQTT telemetry publishing (PV, battery, charging mode, control signal)
✓ Remote battery profile management via MQTT commands
✓ HomeAssistant MQTT Discovery (sensors, select, number entities)
✓ MQTT availability state (Last Will + online announce)
✓ Corrected DPS5005 Modbus register map and measurement unit conversions
✓ Corrected INA226 I2C addresses and shunt resistor configuration
✓ Portable logging shim (`Logger.h`) — ESP_LOG on ESP32, Serial on Arduino, stderr on native
✓ State transition logging in `BatteryManager` with mode names and battery voltage
✓ Rate-limited stale-measurement warnings (configurable via `STALE_LOG_INTERVAL`)
✓ PV power drop/recovery transition logging with correct mW units

### Planned Improvements

| Status | Item | Notes |
|--------|------|-------|
| ✓ | WiFi timeout + standalone fallback | Device charges without WiFi |
| ✓ | Gate MPPT on fresh measurements (DPS) | Only perturbs when new Modbus read arrives |
| ✓ | Variable-step P&O | Eliminates steady-state MPP oscillation |
| ✓ | Hot-reload battery profile fields | No restart on field change via MQTT |
| ✓ | Float telemetry (V/A/W) | HA display quality |
| ✓ | OTA firmware update | Essential for field-deployed devices |
| ✓ | Efficiency calculation (η = P_out/P_in) | Published as telemetry |
| ✓ | Portable logging shim | `Logger.h` — uniform `ESP_LOG*` across all targets |
| [ ] | Load disconnect GPIO | Cut load when battery below `loadDisconnectVoltage_mV` |
| [ ] | Load disconnect hysteresis | Prevent rapid cycling at threshold boundary |
| [ ] | CRC lookup table for DPS Modbus | Optimise CRC16 computation |
| [ ] | Temperature compensation | Thermal derating via external sensor |
| [ ] | INA226 averaging / alert config | Reduce noise on measurements |
| [ ] | Configuration web UI | Runtime tuning without recompilation |

## Integration Notes

Include paths configured in `platformio.ini` (shared across `esp32dev` and `esp32dev-test`):
```
-Ilib
-Ilib/Battery
-Ilib/ChargeController
-Ilib/Measurements
-Ilib/MpptController
-Ilib/Sensors
-Ilib/Actuators
-Ilib/Logger
-Ilib/Mqtt          (esp32dev only)
-Ilib/OtaHandler    (esp32dev only)
```

Build flags:
| Flag | Effect |
|---|---|
| `-D DPS_DC_CONVERTER` | Use DPS5005 Modbus actuator + measurements; omit for PWM + INA226 |
| `-D MQTT_CLIENT` | Enable WiFi + MQTT telemetry and remote profile control |
| `-D MQTT_MAX_PACKET_SIZE=2048` | Required for HomeAssistant discovery JSON payloads |
| `-D CONFIG_LOG_DEFAULT_LEVEL=4` | ESP-IDF log level: 4 = DEBUG (all `ESP_LOGD` messages visible) |

Library dependencies (`esp32dev`):
| Library | Purpose |
|---|---|
| `knolleary/PubSubClient` | MQTT transport |
| `bblanchon/ArduinoJson @ ^7.0.0` | HomeAssistant discovery JSON serialisation |

Main entry point (`main.cpp`) instantiates components for the selected hardware variant and wires them together. All hardware-specific paths are guarded by preprocessor flags.


