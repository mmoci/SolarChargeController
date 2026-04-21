# Hardware Setup

---

## Bill of Materials

| # | Component | Notes |
|---|---|---|
| 1 | **ESP32-DevKit** (any 38-pin or 30-pin board) | Main controller |
| 2 | **DPS5005** DC-DC converter *(DPS variant)* | Modbus RTU over Serial2; provides integrated measurements |
| 3 | **PWM-controlled DC-DC buck converter** *(PWM variant)* | Any converter with a PWM dimming input |
| 4 | **INA226** × 2 *(PWM variant only)* | PV sensor (`0x40`) + Battery sensor (`0x41`) |
| 5 | **Shunt resistors** × 2 — 10 mΩ *(PWM variant only)* | Adjust `PV_SHUNT_mOhm` / `BATTERY_SHUNT_mOhm` in `Config.h` if different |
| 6 | Solar panel | 12–24 V; ensure open-circuit voltage < DPS maximum input |
| 7 | Battery pack | Supported: Li-Ion 3S/4S, LiFePO4 4S, or custom chemistry |

---

## Build Variants

Two mutually exclusive hardware paths are selected at compile time via a build flag in `platformio.ini`.

| Variant | Build flag | Actuator | Measurements source |
|---|---|---|---|
| **DPS** (default) | `-D DPS_DC_CONVERTER` | DPS5005 — voltage/current setpoint via Modbus RTU | Integrated in DPS (`DPSxMeasurements`) |
| **PWM** | *(no flag)* | PWM DC-DC buck — duty cycle via `ledcWrite` | INA226 current/voltage sensors (`SensorINA226`) |

---

## DPS5005 Variant — Wiring

```
ESP32          DPS5005
─────────────────────────
GPIO 16 (RX2)  →  TX
GPIO 17 (TX2)  →  RX
GND            →  GND   (shared reference)

DPS5005 input  →  Solar panel
DPS5005 output →  Battery pack
```

> **Note:** The DPS5005 factory Modbus slave address is `0x01`. If you have changed it on the device menu, update `DPSxDcConverterConfig::SLAVE_ADDRESS` in `DPSxDcConverter.h`.

### DPS5005 Modbus Register Map

| Register | Name | Access | Resolution | Example |
|---|---|---|---|---|
| `0x0000` | `U_SET` | R/W | 0.01 V/bit | 2400 = 24.00 V |
| `0x0001` | `I_SET` | R/W | 0.001 A/bit | 5000 = 5.000 A |
| `0x0002` | `UOUT` | R | 0.01 V/bit | output voltage |
| `0x0003` | `IOUT` | R | 0.01 A/bit | output current |
| `0x0004` | `POWER` | R | 0.1 W/bit | output power |
| `0x0005` | `UIN` | R | 0.01 V/bit | input voltage |
| `0x0006` | `LOCK` | R/W | 0=off 1=on | key-lock |
| `0x0007` | `PROTECT` | R | flags | 0=OK, 1=OVP, 2=OCP, 3=OPP |
| `0x0008` | `CV_CC` | R | 0=CV 1=CC | operating mode (read-only) |
| `0x0009` | `ON_OFF` | R/W | 0=off 1=on | output enable |

---

## PWM + INA226 Variant — Wiring

```
ESP32          INA226 (PV sensor, addr 0x40)
───────────────────────────────────────────
GPIO 21 (SDA)  →  SDA
GPIO 22 (SCL)  →  SCL
3.3 V          →  VCC
GND            →  GND
                   A0  →  GND   (address bit 0)
                   A1  →  GND   (address bit 1)
                   IN+ / IN- →  across PV shunt resistor (10 mΩ)

ESP32          INA226 (Battery sensor, addr 0x41)
─────────────────────────────────────────────────
GPIO 21 (SDA)  →  SDA
GPIO 22 (SCL)  →  SCL
3.3 V          →  VCC
GND            →  GND
                   A0  →  VS    (address bit 0 = 1)
                   A1  →  GND   (address bit 1 = 0)
                   IN+ / IN- →  across Battery shunt resistor (10 mΩ)

ESP32          PWM DC-DC converter
──────────────────────────────────
GPIO 32        →  PWM dimming input
GND            →  GND

DC-DC input    →  Solar panel
DC-DC output   →  Battery pack
```

### INA226 I²C Address Strapping

| A1 | A0 | Address | Usage |
|---|---|---|---|
| GND | GND | `0x40` | PV sensor |
| GND | VS  | `0x41` | Battery sensor |
| VS  | GND | `0x44` | (not used by default) |
| VS  | VS  | `0x45` | (not used by default) |

---

## INA226 Shunt Resistor Sizing

The INA226 maximum differential input is ±81.92 mV.

| Shunt | Max current before saturation |
|---|---|
| 10 mΩ | 8.19 A |
| 5 mΩ | 16.38 A |
| 2 mΩ | 40.96 A |

Change `SensorConfig::SensorINA226::PV_SHUNT_mOhm` and `BATTERY_SHUNT_mOhm` in `lib/Config.h` to match your actual shunt values.

---

## PWM Converter — Pin Configuration

| Parameter | Value (default) | Where to change |
|---|---|---|
| GPIO pin | 32 | `InitializerPwm::PWM_PIN` |
| LEDC channel | 0 | `PwmDcConverterConfig::DEFAULT_LED_CHANNEL` |
| Frequency | 10 000 Hz | `PwmDcConverterConfig::DEFAULT_FREQUENCY` |
| Resolution | 8 bit | `PwmDcConverterConfig::DEFAULT_RESOLUTION` |

---

## Power Considerations

- The ESP32 must share a **common GND** with all sensors and actuators.
- Use a separate 3.3 V regulator or the onboard one on the DevKit for the ESP32 supply.
- Isolate high-current paths (solar / battery) from the ESP32 logic supply.
- The DPS5005 requires a separate 5 V supply for its logic side (via its own USB port) if you want the output relay to close before the input voltage rises.
