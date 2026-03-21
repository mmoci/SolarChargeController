
# Solar Charge Controller - Library Components

This directory contains the project-specific libraries for an **ESP32-based MPPT (Maximum Power Point Tracking) Solar Charge Controller**. The modular architecture enables flexible support for different sensor and actuator configurations.

## Project Overview

The Solar Charge Controller is a sophisticated embedded system designed to optimize energy harvesting from solar panels and manage battery charging. It implements:

- **MPPT Algorithm**: Continuously tracks and extracts maximum power point from solar panels
- **Multi-Stage Battery Charging**: Idle → Precharge → CC (Constant Current) → CV (Constant Voltage) → Done
- **Flexible Hardware Architecture**: Supports multiple sensor and actuator implementations
- **Safety Management**: Fault detection and handling
- **Real-time Control**: 3ms update loop with PI (Proportional & Integral) control

## Platform & Configuration

- **Target Board**: ESP32-DevKit
- **Framework**: Arduino
- **Language Standard**: C++17
- **Default Actuator**: DPS DC-DC Converter (with PWM fallback)
- **Communication**: I2C for sensors, Serial (115200 baud) for debugging

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

### Supporting Components

#### **Measurements Interface** (`Measurements/`)
- `MeasurementsIf`: Base interface for sensor abstraction
- Voltage and current data structures
- Power calculation support

#### **Configuration** (`Config.h`)
Build-time and runtime configuration constants

#### **Utilities** (`Utility.h`)
Helper functions and common utilities

## Build Targets

### Production Target: `esp32dev`
- Compiles for ESP32 hardware
- Full MPPT charging controller implementation
- Default uses DPS DC-Converter (`-D DPS_DC_CONVERTER` flag)
- Optimized for C++17

### Test Target: `esp32dev-test`
- Native platform (desktop testing)
- Google Test framework integration
- Isolated component unit testing without hardware
- Battery manager, charge controller, and MPPT algorithm tests included

## Architecture Highlights

### Design Patterns

1. **Dependency Injection**: Core components receive sensor and actuator dependencies
2. **Interface Abstraction**: Hardware implementations behind `MeasurementsIf` and `ActuatorIf`
3. **Composition Pattern**: ChargeController composes Battery, MPPT, and measurements
4. **Compile-time Configuration**: Build flags enable/disable hardware variants

### Data Flow

```
Solar Panel → PV Sensors → ChargeController → MPPT Algorithm → PWM/DPS Actuator → Battery
                                    ↓
                           BatteryManager → Battery Sensors
                                    ↓
                          Battery State Machine
```

## Testing

The project includes comprehensive unit tests:
- `test_battery_manager.cpp`: Battery state machine and charging mode transitions
- `test_charge_controller.cpp`: Main controller logic and MPPT integration
- `test_mppt_controller.cpp`: MPPT algorithm verification
- `test_utility.cpp`: Helper function validation

Mock implementations provided for testing without hardware.

## Current Status & Future Improvements

### Features Implemented
✓ Modular sensor/actuator architecture
✓ Multi-stage battery charging
✓ MPPT algorithm implementation
✓ PI control for voltage/current limiting
✓ Dual hardware support (PWM & DPS)
✓ Unit testing framework

### Planned Improvements
- [ ] CRC lookup table optimization for data integrity
- [ ] Program configuration registry for dynamic object initialization
- [ ] Extended telemetry and data logging
- [ ] Wifi connectivity for remote monitoring

### Suggested Enhancement Areas:
- MQTT Telemetry Integration - Publish measurements & state to broker
- Dynamic Battery Profile Selection - EEPROM storage + runtime switching
- Temperature Compensation - Sensor integration for thermal derating
- Extended Diagnostics - Error logging, efficiency tracking, performance metrics
- Configuration Web UI - Runtime tuning without recompilation
- INA226 Optimization - Averaging, alert thresholds, measurement validation

## Integration Notes

Include files are configured via `platformio.ini`:
```
-Ilib/Battery
-Ilib/ChargeController
-Ilib/Measurements
-Ilib/MpptController
-Ilib/Sensors
-Ilib/Actuators
```

Main entry point (`main.cpp`) demonstrates instantiation of both PWM and DPS-based configurations with compile-time selection via `DPS_DC_CONVERTER` preprocessor flag.


