# MQTT and Home Assistant Integration

MQTT support is enabled by the `-D MQTT_CLIENT` build flag. When enabled, the device connects to a WiFi network and an MQTT broker, publishes real-time telemetry, and registers itself with Home Assistant via MQTT Discovery.

---

## Prerequisites

- MQTT broker (e.g. [Mosquitto](https://mosquitto.org/), or the built-in HA MQTT broker add-on)
- Home Assistant with the MQTT integration enabled
- WiFi credentials and broker address in `include/secrets.h`

---

## Topic Layout

All topics are prefixed with `solar/{deviceId}/` where `deviceId` is set in `main.cpp`:

```cpp
static constexpr std::string_view DEVICE_ID {"solar_controller_1"};
```

### Availability

| Topic | Direction | Payload | Notes |
|---|---|---|---|
| `solar/{deviceId}/availability` | Controller → HA | `online` / `offline` | `offline` is the MQTT Last Will; `online` is published on every (re)connect |

### Telemetry (published every 5 s)

| Topic | Unit | Description |
|---|---|---|
| `solar/{deviceId}/telemetry/pv_voltage` | V (float) | Solar panel voltage |
| `solar/{deviceId}/telemetry/pv_current` | A (float) | Solar panel current |
| `solar/{deviceId}/telemetry/pv_power` | W (float) | Solar panel power (V × I) |
| `solar/{deviceId}/telemetry/battery_voltage` | V (float) | Battery terminal voltage |
| `solar/{deviceId}/telemetry/battery_current` | A (float) | Battery charging current |
| `solar/{deviceId}/telemetry/charging_mode` | string | `Idle` / `Precharge` / `CC` / `CV` / `Done` / `Fault` |
| `solar/{deviceId}/telemetry/control_signal_pct` | % (int) | MPPT duty cycle / setpoint percentage |
| `solar/{deviceId}/telemetry/efficiency_pct` | % (float) | Converter efficiency η = P_out / P_in |

### Battery Profile State (retained, published on every (re)connect)

| Topic | Payload | Description |
|---|---|---|
| `solar/{deviceId}/profile/battery_type/state` | string | Current battery type name |
| `solar/{deviceId}/profile/max_voltage/state` | integer (mV) | Current max voltage |
| `solar/{deviceId}/profile/recharge_voltage/state` | integer (mV) | Current recharge voltage |
| `solar/{deviceId}/profile/precharge_voltage/state` | integer (mV) | Current precharge voltage |
| `solar/{deviceId}/profile/load_disconnect_voltage/state` | integer (mV) | Current load disconnect voltage |
| `solar/{deviceId}/profile/max_charging_current/state` | integer (mA) | Current max charging current |

### Battery Profile Commands (subscribed by controller)

| Topic | Expected payload | Description |
|---|---|---|
| `solar/{deviceId}/profile/battery_type/set` | `LIION_3S` / `LIION_4S` / `LIFEPO4_4S` / `CUSTOM` | Change battery type (triggers device restart) |
| `solar/{deviceId}/profile/max_voltage/set` | integer in mV | Override max voltage |
| `solar/{deviceId}/profile/recharge_voltage/set` | integer in mV | Override recharge voltage |
| `solar/{deviceId}/profile/precharge_voltage/set` | integer in mV | Override precharge voltage |
| `solar/{deviceId}/profile/load_disconnect_voltage/set` | integer in mV | Override load disconnect voltage |
| `solar/{deviceId}/profile/max_charging_current/set` | integer in mA | Override max charging current |

### HomeAssistant Discovery

```
homeassistant/{component}/{deviceId}/{objectId}/config
```

Published on every (re)connect with `retain=true`.

---

## Home Assistant Auto-Discovery

When the device connects, `MqttSolarControllerBridge::publishDiscovery()` publishes discovery configs for:

### Sensor entities (7)

| Entity | Unit |
|---|---|
| PV Voltage | V |
| PV Current | A |
| PV Power | W |
| Battery Voltage | V |
| Battery Current | A |
| Charging Mode | — |
| Control Signal | % |

### Select entity (1)

| Entity | Options |
|---|---|
| Battery Type | LIION_3S, LIION_4S, LIFEPO4_4S, CUSTOM |

### Number entities (5)

| Entity | Unit | Range |
|---|---|---|
| Max Voltage | mV | 0 – 20 000 |
| Recharge Voltage | mV | 0 – 20 000 |
| Precharge Voltage | mV | 0 – 20 000 |
| Load Disconnect Voltage | mV | 0 – 20 000 |
| Max Charging Current | mA | 0 – 10 000 |

All 13 entities are grouped under a single HA device entry with the device ID as both name and identifier.

---

## Command Handling Flow

```
HA publishes to solar/{deviceId}/profile/max_voltage/set  "12800"
    │
    ▼
MqttClient dispatches to registered callback
    │
    ▼
MqttSolarControllerBridge::handleIntCommand()
    │
    ├── parseIntSafe("12800")  → 12800
    ├── profileSelector.setMaxVoltage(12800)
    │       ├── validate (±10% of type default)
    │       ├── save to NVS
    │       └── notify observer → BatteryManager::updateBatteryProfile()  (hot-reload)
    └── bridge publishes updated state topics (retained)

HA publishes to solar/{deviceId}/profile/battery_type/set  "LIFEPO4_4S"
    │
    ▼
MqttSolarControllerBridge::onBatteryTypeSet()
    │
    ├── stringToBatteryType("LIFEPO4_4S")  → LIFEPO4_4S
    ├── profileSelector.setProfileType(LIFEPO4_4S)
    │       ├── load LIFEPO4_4S defaults
    │       ├── validate
    │       └── save to NVS
    └── esp_restart()   ← device reboots with new profile
```

---

## Availability and Last Will

The MQTT Last Will & Testament (LWT) is configured at connection time:

- **LWT topic:** `solar/{deviceId}/availability`
- **LWT payload:** `offline`

On every successful (re)connect, the bridge immediately publishes `online` to the same topic. Home Assistant uses this topic for the device availability indicator.

---

## WiFi and MQTT Reconnection

The `MqttClient` handles all reconnection automatically:

| Event | Behaviour |
|---|---|
| WiFi unavailable at boot | Continues without WiFi; retries every 30 s in the background |
| WiFi drops during operation | Retries every 30 s |
| MQTT broker unreachable | Retries every 5 s once WiFi is available |
| MQTT broker disconnects | Republishes all subscriptions and retained state on reconnect |

---

## Multiple Devices

To run multiple solar controllers on the same broker, give each one a unique device ID:

```cpp
// Device 1
static constexpr std::string_view DEVICE_ID {"solar_controller_1"};

// Device 2
static constexpr std::string_view DEVICE_ID {"solar_controller_2"};
```

Each device gets its own set of MQTT topics and its own HA device entry.

---

## Testing MQTT Without Home Assistant

Use any MQTT client (e.g. [MQTT Explorer](https://mqtt-explorer.com/) or `mosquitto_sub`) to monitor the device:

```bash
# Subscribe to all topics for the device
mosquitto_sub -h 192.168.1.10 -t "solar/solar_controller_1/#" -v

# Send a battery type change command
mosquitto_pub -h 192.168.1.10 -t "solar/solar_controller_1/profile/battery_type/set" -m "LIFEPO4_4S"
```
