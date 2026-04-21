# Configuration

All compile-time and runtime configuration lives in two places:

- **`lib/Config.h`** — sensor addresses, control gains, battery profile constants
- **`platformio.ini` build flags** — selects hardware variant and optional features

---

## Build Flags (`platformio.ini`)

| Flag | Effect | Default |
|---|---|---|
| `-D DPS_DC_CONVERTER` | Use `DPSxDcConverter` (Modbus RTU) + `DPSxMeasurements` | **On** |
| `-D MQTT_CLIENT` | Enable WiFi, MQTT telemetry, HA discovery, OTA | **On** |
| `-D MQTT_MAX_PACKET_SIZE=2048` | Needed for HomeAssistant discovery JSON payloads | **On** |

To compile without MQTT (standalone charging only), remove `-D MQTT_CLIENT` from `build_flags`.  
To use the PWM + INA226 path, remove `-D DPS_DC_CONVERTER`.

---

## `lib/Config.h` — Sensor Configuration

```cpp
namespace SensorConfig
{
    namespace SensorINA226
    {
        constexpr int PV_SHUNT_mOhm      = 10;  // mΩ — PV channel shunt
        constexpr int BATTERY_SHUNT_mOhm = 10;  // mΩ — Battery channel shunt
    }

    // I²C addresses (set by A0/A1 pin strapping)
    static constexpr uint16_t PV_SENSOR_DEVICE_ADDRESS      { 0x40 }; // A0=GND, A1=GND
    static constexpr uint16_t BATTERY_SENSOR_DEVICE_ADDRESS { 0x41 }; // A0=VS,  A1=GND
}
```

**Changing shunt values:** INA226 saturation current = 81.92 mV ÷ shunt_Ω. At 10 mΩ that is 8.19 A per channel.

---

## `lib/Config.h` — Charge Controller Configuration

```cpp
namespace ChargeControllerConfig
{
    // PV power threshold below which charging is considered unavailable (1 W)
    static constexpr long PV_POWER_THRESHOLD           { 1000 };   // mW

    // How long PV power must stay below threshold before charging is disabled
    static constexpr long PV_POWER_UNAVAILABLE_TIMEOUT { 10000 };  // ms (10 s)

    // Minimum interval between repeated stale-measurement log warnings
    static constexpr unsigned long STALE_LOG_INTERVAL  { 10000 };  // ms (10 s)

    // PI controller gains for voltage and current clamping
    static constexpr double Kp               { 1.0  };
    static constexpr double Ki               { 0.01 };
    static constexpr long   MAX_INTEGRAL_ERROR { 500 };
}
```

### PI Tuning Notes

- `Kp = 1.0`, `Ki = 0.01` are tuned for typical Li-Ion packs at the default DPS Modbus cycle time (~100 ms).
- If you observe oscillation in CV mode, reduce `Kp` or `Ki`.
- If steady-state error is too large (battery never quite reaches `maxVoltage`), increase `Ki`.
- `MAX_INTEGRAL_ERROR` prevents integral wind-up from causing large overshoots.

---

## `lib/Config.h` — Built-in Battery Profiles

All voltage values are in **millivolts (mV)**, all current values in **milliamps (mA)**.

### LI_ION_3S_DEFAULT

| Field | Value |
|---|---|
| `maxVoltage_mV` | 12 600 (4.20 V × 3 cells) |
| `rechargeVoltage_mV` | 12 400 |
| `prechargeVoltage_mV` | 9 600 |
| `minSafeVoltage_mV` | 9 000 |
| `loadDisconnectVoltage_mV` | 9 600 |
| `maxChargingCurrent_mA` | 10 000 (10 A) |
| `cutoffCurrent_mA` | 100 |
| `prechargeCurrent_mA` | 500 |
| `idleCurrent_mA` | 100 |

### LI_ION_4S_DEFAULT

| Field | Value |
|---|---|
| `maxVoltage_mV` | 16 800 (4.20 V × 4 cells) |
| `rechargeVoltage_mV` | 16 500 |
| `prechargeVoltage_mV` | 12 800 |
| `minSafeVoltage_mV` | 12 000 |
| `loadDisconnectVoltage_mV` | 12 800 |
| `maxChargingCurrent_mA` | 10 000 (10 A) |
| `cutoffCurrent_mA` | 100 |
| `prechargeCurrent_mA` | 500 |
| `idleCurrent_mA` | 100 |

### LIFEPO4_4S_DEFAULT

| Field | Value |
|---|---|
| `maxVoltage_mV` | 14 000 (3.50 V × 4 cells) |
| `rechargeVoltage_mV` | 13 800 |
| `prechargeVoltage_mV` | 10 000 |
| `minSafeVoltage_mV` | 9 000 |
| `loadDisconnectVoltage_mV` | 10 000 |
| `maxChargingCurrent_mA` | 10 000 (10 A) |
| `cutoffCurrent_mA` | 100 |
| `prechargeCurrent_mA` | 500 |
| `idleCurrent_mA` | 100 |

### CUSTOM_DEFAULT

Same defaults as `LI_ION_4S_DEFAULT`. Use this type when you want full control over every field without any chemistry-specific defaults interfering.

---

## `include/Secrets.h` — Runtime Credentials

Never committed to source control. Copy `include/Secrets.h.template`:

```cpp
#define MQTT_BROKER    "192.168.1.10"
#define MQTT_USERNAME  "homeassistant"
#define MQTT_PASSWORD  "secret"
#define WIFI_SSID      "MyNetwork"
#define WIFI_PASSWORD  "MyPassword"
#define OTA_PASSWORD   "otasecret"
```

---

## `main.cpp` — Static IP Configuration

The ESP32 is configured with a static IP address to avoid DHCP latency at boot (reduces time to first MQTT message):

```cpp
MqttClient::Config mqttConfig{
    ...
    .staticIp  = IPAddress(192, 168, 1, 50),
    .gateway   = IPAddress(192, 168, 1, 1),
    .subnet    = IPAddress(255, 255, 255, 0),
    .dns1      = IPAddress(192, 168, 1, 1),
};
```

Change these values to match your network. Remove all four `IPAddress` fields to use DHCP (the `MqttClient` falls back automatically when `staticIp` is `0.0.0.0`).

---

## `main.cpp` — Device ID

```cpp
static constexpr std::string_view DEVICE_ID {"solar_controller_1"};
```

The device ID appears in every MQTT topic and in the HA device registry. Change it if you run multiple controllers on the same broker.

---

## DPS5005 Actuator Constants

Defined in `lib/Actuators/DPSxDcConverter.h`:

| Constant | Default | Meaning |
|---|---|---|
| `SLAVE_ADDRESS` | `0x01` | Modbus slave address |
| `MESSAGE_TMO` | 100 ms | Wait time for a Modbus response |
| `MAX_READS_BEFORE_WRITE` | 3 | Read cycles between write cycles |
| `MAX_SOFT_STEP` | 1 | Max setpoint change per Modbus cycle |
| `ERROR_RECOVERY_TMO` | 10 000 ms | Pause after consecutive errors |
| `CONSECUTIVE_ERRORS_THRESHOLD` | 5 | Errors before entering recovery pause |
| `MAX_MPPT_VOLTAGE_CONTROL_VALUE` | 5000 | 50.00 V at 0.01 V/bit |
| `MAX_MPPT_CURRENT_CONTROL_VALUE` | 5000 | 5.000 A at 0.001 A/bit |

## PWM Actuator Constants

Defined in `lib/Actuators/PwmDcConverter.h`:

| Constant | Default | Meaning |
|---|---|---|
| `DEFAULT_RESOLUTION` | 8 bit | `ledcWrite` resolution |
| `DEFAULT_LED_CHANNEL` | 0 | ESP32 LEDC channel |
| `DEFAULT_FREQUENCY` | 10 000 Hz | PWM frequency |
| `MAX_SOFT_STEP` | 5 | Max duty cycle change per loop tick |
