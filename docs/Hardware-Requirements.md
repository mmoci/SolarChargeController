# Hardware Requirements

## Components

| Component | Details |
|---|---|
| **Microcontroller** | ESP32-DevKit (any ESP32 dev board) |
| **Actuator (default)** | DPS5005 DC-DC converter connected via Serial2 (Modbus RTU, 9600 baud) |
| **Actuator (alternative)** | Any PWM-controlled DC-DC buck converter |
| **PV Sensor** | INA226 current/voltage sensor (I2C address `0x40`) — required only with PWM actuator |
| **Battery Sensor** | INA226 current/voltage sensor (I2C address `0x41`) — required only with PWM actuator |
| **Shunt Resistors** | 10 mΩ (default, configurable in `Config.h`) — used only with INA226 sensors |
| **Communication** | Serial2 for DPS (Modbus RTU, 9600 baud); I2C for INA226 (PWM variant only); Serial (115200 baud) for debug |
| **Networking** | Built-in ESP32 WiFi — only required when `MQTT_CLIENT` is enabled |

## Build Variants

| Build flag | Actuator | Measurement source |
|---|---|---|
| `-D DPS_DC_CONVERTER` (default) | DPS5005 via Modbus RTU | Integrated in DPS — no INA226 needed |
| *(no flag)* | PWM DC-DC buck converter | INA226 sensors (PV @ `0x40`, battery @ `0x41`) |

## INA226 Address Strapping *(PWM variant only)*

- **A0=GND, A1=GND** → `0x40` (PV sensor)
- **A0=VS, A1=GND** → `0x41` (Battery sensor)
