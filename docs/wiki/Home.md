# Solar Charge Controller — Wiki

Welcome to the full documentation for the **ESP32-based MPPT Solar Charge Controller**.

---

## What is This Project?

This project is an embedded firmware for an **ESP32 microcontroller** that turns it into a fully-featured solar charge controller. It continuously harvests maximum power from a solar panel (via MPPT), manages multi-stage battery charging, and optionally reports real-time telemetry to **Home Assistant** over MQTT.

---

## Key Features

| Feature | Details |
|---|---|
| **MPPT algorithm** | Variable-step Perturb & Observe — maximises harvested solar power |
| **Multi-stage charging** | Idle → Precharge → CC → CV → Done (plus Fault safety state) |
| **PI control** | Proportional + Integral clamp for voltage and current limits |
| **Soft ramp** | Prevents sudden duty-cycle jumps on every actuator type |
| **Dual actuator support** | DPS5005 via Modbus RTU **or** any PWM DC-DC buck converter |
| **Dual sensor support** | Integrated DPS measurements **or** external INA226 I²C sensors |
| **Battery profiles** | 3 built-in chemistries + CUSTOM; stored in NVS flash, survives reboots |
| **MQTT telemetry** | PV/battery voltage, current, power, efficiency, charging mode — every 5 s |
| **Remote profile control** | Change any profile field from Home Assistant without touching the device |
| **HA MQTT Discovery** | Auto-registers sensors, select, and number entities |
| **OTA firmware updates** | Flash new firmware over WiFi via Arduino IDE or PlatformIO |
| **Unit-tested** | 61 test cases on a native (desktop) build target |

---

## Wiki Pages

| Page | Contents |
|---|---|
| [Architecture](Architecture.md) | System design, component relationships, data flow, init/loop order |
| [Hardware Setup](Hardware-Setup.md) | Bill of materials, wiring diagram, pin assignments |
| [Build and Flash](Build-and-Flash.md) | PlatformIO build, flash, serial monitor, running unit tests |
| [Configuration](Configuration.md) | Build flags, `Config.h` constants, `secrets.h` template |
| [Battery Profiles](Battery-Profiles.md) | Built-in profiles, custom profile, NVS persistence, MQTT override |
| [Charging State Machine](Charging-State-Machine.md) | All charging modes and every state transition |
| [MPPT Algorithm](MPPT-Algorithm.md) | P&O algorithm, PI clamping, soft ramp, DPS gating |
| [MQTT and Home Assistant](MQTT-and-HomeAssistant.md) | Topic layout, discovery, availability, command handling |
| [OTA Updates](OTA-Updates.md) | WiFi OTA setup, upload from IDE, safety behaviour |
| [Testing](Testing.md) | Test targets, mock infrastructure, time control helpers |

---

## Quick Start

1. **Clone** the repository.
2. Copy `include/secrets.h.template` → `include/secrets.h` and fill in your WiFi / MQTT credentials.
3. Open in **PlatformIO** (VS Code extension or CLI).
4. Select the hardware variant:
   - DPS5005 (default): keep `-D DPS_DC_CONVERTER` in `platformio.ini`.
   - PWM + INA226: remove that flag and wire the INA226 sensors.
5. Run **Upload** (`pio run -e esp32dev -t upload`).
6. Open the **Serial Monitor** at 115 200 baud to watch the boot log.
7. If MQTT is enabled, check Home Assistant → Settings → Devices for the auto-discovered device.

See [Build and Flash](Build-and-Flash.md) for full details.
