# Architecture

This page explains how all the components in the firmware fit together.

---

## Component Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                          main.cpp                                │
│  setup() / loop()                                                │
│                                                                  │
│  ┌──────────────┐   ┌──────────────────────┐   ┌─────────────┐  │
│  │  MqttClient  │◄──│ MqttSolarController  │   │ OtaHandler  │  │
│  │  (transport) │   │     Bridge (domain)  │   │  (OTA WiFi) │  │
│  └──────────────┘   └──────────────────────┘   └─────────────┘  │
│                              │                                   │
│                    ┌─────────▼──────────┐                        │
│                    │  BatteryProfile    │                        │
│                    │    Selector (NVS)  │                        │
│                    └─────────┬──────────┘                        │
│                              │profile                           │
│                    ┌─────────▼──────────┐                        │
│                    │  ChargeController  │                        │
│                    │  (orchestrator)    │                        │
│                    └──┬──────┬──────┬───┘                        │
│         pvMeas        │      │      │  batteryMeas               │
│  ┌────────────────┐   │      │      │  ┌──────────────────┐      │
│  │ MeasurementsIf │◄──┘      │      └──► MeasurementsIf   │      │
│  │ (PV)           │          │         │ (Battery)        │      │
│  └────────────────┘          │         └──────────────────┘      │
│                         ┌────▼────┐                              │
│                         │ MPPT    │                              │
│                         │ Control │                              │
│                         └────┬────┘                              │
│                         ┌────▼────┐                              │
│                         │Actuator │                              │
│                         │   If   │                              │
│                         └─────────┘                              │
└──────────────────────────────────────────────────────────────────┘
```

---

## Modules

### `ChargeController` — main orchestrator

- Reads fresh PV and battery measurements every loop tick.
- Calls `updatePvAvailability()` to decide whether charging is available.
- Gates MPPT perturbations on genuinely new hardware readings (`lastTimeUpdated()`).
- Runs `BatteryManager::update()` to advance the charging state machine.
- Applies current / voltage PI clamps when the battery manager requests them.
- Applies a **soft ramp** to the final control value before writing to the actuator.

### `MpptController` — P&O algorithm

- Stateless except for the last measurement snapshot.
- Uses variable step size scaled by `|dP/dV|` to converge quickly far from MPP and oscillate minimally at MPP.
- Range: 0–100 % (normalised; scaled to device units by the actuator).

### `BatteryManager` — charging state machine

- Six modes: `Idle`, `Precharge`, `CC`, `CV`, `Done`, `Fault`.
- All voltage thresholds come from the injected `BatteryProfile`.
- `Fault` is a terminal, non-recoverable state.
- Logs every mode transition with the battery voltage at the time.

### `BatteryProfileSelector` — NVS persistence

- Loads the active profile from ESP-IDF NVS flash on `init()`.
- Falls back to the `LIION_3S` default if NVS is empty or corrupt.
- Exposes individual setters (`setMaxVoltage()`, `setRechargeVoltage()`, …).
- Each setter validates the value within ±10 % of the type default before writing.
- Calls a registered observer callback after every successful change so `BatteryManager` hot-reloads the new values.

### `Initializer` — hardware singleton

Abstract singleton with two concrete implementations selected at compile time:

| Concrete class | Build flag | Actuator | Sensors |
|---|---|---|---|
| `InitializerDps` | `-D DPS_DC_CONVERTER` | `DPSxDcConverter` (Modbus RTU) | `DPSxMeasurements` (from DPS) |
| `InitializerPwm` | *(none)* | `PwmDcConverter` (GPIO PWM) | `SensorINA226` × 2 (I²C) |

### `MqttClient` — MQTT transport

- Manages WiFi connection with a configurable timeout; if WiFi is unavailable the device enters standalone charging mode and retries in the background.
- Manages MQTT reconnection with 5 s backoff.
- Supports multiple `onConnect` callbacks (additive).
- All subscriptions are replayed on every reconnect.
- Uses Last Will & Testament for availability state.

### `MqttSolarControllerBridge` — domain bridge

- Knows nothing about WiFi or reconnection details.
- Registers an `onConnect` callback that publishes HA discovery configs and retained profile state.
- Publishes telemetry every 5 s.
- Handles incoming profile command messages: validate → persist to NVS → ESP restart.

### `OtaHandler` — over-the-air updates

- Wraps `ArduinoOTA`.
- Drives the actuator to 0 before flash begins (safety).
- Only compiled when `-D MQTT_CLIENT` is set (OTA requires WiFi).

---

## Design Patterns

| Pattern | Where used |
|---|---|
| **Dependency injection** | `ChargeController` receives pointers to `MeasurementsIf` and `ActuatorIf` |
| **Interface abstraction** | `MeasurementsIf`, `ActuatorIf` decouple hardware from algorithm |
| **Singleton** | `Initializer::getInstance()` returns the correct concrete hardware class |
| **Observer / callback** | `BatteryProfileSelector::registerProfileObserver()` notifies `BatteryManager` |
| **Compile-time feature flags** | `-D DPS_DC_CONVERTER`, `-D MQTT_CLIENT` select hardware/feature variants |
| **Type + Overrides** | `BatteryProfileSelector` stores only field overrides in NVS — type defaults are always compiled in |
| **Layered MQTT** | Transport (`MqttClient`) separated from domain (`MqttSolarControllerBridge`) |

---

## Data Flow

```
Solar Panel
    │ PV voltage + current
    ▼
MeasurementsIf (PV)
    │
    ▼
ChargeController::update()
    │
    ├── updatePvAvailability()    →  isChargingAvailable flag
    │
    ├── MpptController::update()  →  requestedOutput (0-100)
    │
    ├── BatteryManager::update()  →  mode, voltage/current limits
    │       │
    │       └── reads BatteryProfile (from BatteryProfileSelector)
    │
    ├── clampLimitPI()  →  voltage clamp (CV mode)
    ├── clampLimitPI()  →  current clamp (CC / Precharge mode)
    ├── softRampControl()  →  smooth transition
    │
    └── ActuatorIf::applyControl()
            │
            └── DPSxDcConverter  (sets voltage/current setpoint via Modbus RTU)
                OR
                PwmDcConverter   (sets duty cycle via ledcWrite)

Battery
    │ battery voltage + current
    ▼
MeasurementsIf (Battery)
    │
    └── ChargeController (feedback loop)

MQTT Broker
    │ /profile/*/set  commands
    ▼
MqttSolarControllerBridge
    │
    └── BatteryProfileSelector::set*()  →  NVS  →  esp_restart()

ChargeController
    │ telemetry every 5 s
    ▼
MqttSolarControllerBridge::publishTelemetry()
    │
    └── MqttClient::publish()  →  MQTT Broker  →  Home Assistant
```

---

## `setup()` Initialisation Order

Order is significant — each step depends on the one before it.

```
1. Serial.begin(115200)               // serial debug output
2. esp_log_level_set("*", DEBUG)      // enable all log levels
3. mqttClient.init()                  // connect WiFi (best-effort, 10 s timeout)
                                      // configure MQTT broker endpoint
4. bridge.init()                      // register onConnect callback + command subscriptions
5. Initializer::getInstance().init()  // init hardware (DPS Serial2 OR INA226 I²C + PWM)
6. controller.init()                  // init BatteryProfileSelector (NVS load), BatteryManager, MpptController
7. otaHandler.init()                  // register ArduinoOTA callbacks and start service
```

## `loop()` Execution Order

```
1. mqttClient.process()         // drive MQTT reconnection + incoming message dispatch
2. otaHandler.handle()          // drive OTA state machine
3. Initializer::update()        // read fresh sensor measurements / send Modbus poll
4. controller.update()          // MPPT + battery state machine + actuator write
5. bridge.publishTelemetry()    // publish to MQTT if 5 s has elapsed
   (every 3 ms loop delay)
```
