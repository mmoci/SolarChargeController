# Charging State Machine

The `BatteryManager` class implements a six-mode state machine that controls whether — and how — the battery is charged.

---

## Modes

| Mode | Charging allowed? | Active limit | Description |
|---|---|---|---|
| `Idle` | ✗ | — | Waiting for charging to become available |
| `Precharge` | ✓ | Current (500 mA max) | Battery voltage is very low; gentle recovery charge |
| `CC` | ✓ | Current (`maxChargingCurrent_mA`) | Constant Current — bulk charging phase |
| `CV` | ✓ | Voltage (`maxVoltage_mV`) | Constant Voltage — absorption phase, current tapering |
| `Done` | ✗ | — | Battery fully charged; waiting for discharge |
| `Fault` | ✗ | — | Safety fault — terminal, non-recoverable |

---

## State Transition Diagram

```
            ┌─────────────────────────────────────────────────────────────────┐
            │  Any mode (except Fault)                                        │
            │  voltage ≤ minSafeVoltage_mV  →  Fault (terminal)              │
            └─────────────────────────────────────────────────────────────────┘

                         chargingAvailable = true
    ┌──────┐          V > prechargeVoltage          ┌────┐
    │ Idle │─────────────────────────────────────────► CC │
    │      │◄─── chargingUnavailableTimer > 60 s ────┤    │
    │      │                                         └─┬──┘
    │      │  V ≤ prechargeVoltage AND V > minSafe      │ V ≥ maxVoltage
    │      │  AND chargingAvailable                     ▼
    │      │──────────────────────────────────────► ┌────┐
    │      │◄─── chargingUnavailableTimer > 60 s ───┤ CV │
    └──────┘                                        └─┬──┘
                                                      │ V ≥ maxVoltage
            ┌──────────┐                              │ AND I ≤ cutoffCurrent_mA
            │Precharge │◄──── V > prechargeV          ▼
            │          │      AND chargingAvailable  ┌──────┐
            │          │                             │ Done │
            │          │◄── chargingUnavailableTimer │      │
            │          │    > 60 s ───────────────── │      │
            └──────────┘                             └──────┘
                                                       │ V ≤ rechargeVoltage
                                                       │ AND chargingAvailable
                                                       └──► CC
```

---

## Transition Rules (per mode)

### Idle

| Condition | Next mode |
|---|---|
| `voltage ≤ minSafeVoltage_mV` | **Fault** |
| `voltage ≤ prechargeVoltage_mV` AND `voltage > minSafeVoltage_mV` AND `chargingAvailable` | **Precharge** |
| `voltage > prechargeVoltage_mV` AND `chargingAvailable` | **CC** |

### Precharge

| Condition | Next mode |
|---|---|
| `voltage ≤ minSafeVoltage_mV` | **Fault** |
| `!chargingAvailable` AND timer > 60 s | **Idle** |
| `voltage > prechargeVoltage_mV` AND `chargingAvailable` | **CC** |

### CC (Constant Current)

| Condition | Next mode |
|---|---|
| `voltage ≤ minSafeVoltage_mV` | **Fault** |
| `!chargingAvailable` AND timer > 60 s | **Idle** |
| `voltage ≥ maxVoltage_mV` AND `chargingAvailable` | **CV** |

### CV (Constant Voltage)

| Condition | Next mode |
|---|---|
| `voltage ≤ minSafeVoltage_mV` OR `voltage > maxVoltage_mV × 1.05` | **Fault** |
| `!chargingAvailable` AND timer > 60 s | **Idle** |
| `voltage ≥ maxVoltage_mV` AND `current ≤ cutoffCurrent_mA` | **Done** |

> The 105 % over-voltage fault catches a runaway condition where the actuator keeps pushing beyond the CV setpoint.

### Done

| Condition | Next mode |
|---|---|
| `voltage ≤ minSafeVoltage_mV` OR `voltage > maxVoltage_mV × 1.05` | **Fault** |
| `!chargingAvailable` AND timer > 60 s | **Idle** |
| `voltage ≤ rechargeVoltage_mV` AND `chargingAvailable` | **CC** |

### Fault

No transitions out. **Fault is a terminal state.** A device restart is required to clear it. This is intentional — a fault indicates a hardware or wiring problem that must be physically inspected.

---

## Charging Disabled Timer

When `chargingAvailable` transitions from `true` to `false` (e.g. the sun goes down), a timer starts. If charging remains unavailable for more than **60 seconds**, any active charging mode (`Precharge`, `CC`, `CV`, `Done`) transitions to `Idle`. This prevents the state machine from being stuck in a charging mode during a multi-hour night.

The timer resets immediately when `chargingAvailable` becomes `true` again.

---

## `isChargingAvailable` (PV Power Gate)

`ChargeController` feeds a boolean `chargingAvailable` to `BatteryManager`. This flag becomes `false` when PV power has been below **1 W** for more than **10 seconds** (configurable in `Config.h`).

---

## API Summary

```cpp
batteryManager.getMode()                    // → BatteryManager::Mode
batteryManager.isChargingAllowed()          // true in Precharge, CC, CV
batteryManager.isCurrentLimitActive()       // true in CC and Precharge
batteryManager.isVoltageLimitActive()       // true in CV
batteryManager.isLoadDisconnectVoltageLimitActive(voltage_mV)  // true when Vbatt < loadDisconnectVoltage
batteryManager.getMaxChargingCurrentLimit() // → std::optional<int> (mA) — set in CC/Precharge
batteryManager.getMaxVoltageLimit()         // → std::optional<int> (mV) — set in CV
```

---

## Logging

Every mode transition is logged at INFO level:

```
[INFO] BatteryManager: Mode: Idle -> CC (Vbatt=11200mV)
[INFO] BatteryManager: Mode: CC -> CV (Vbatt=12600mV)
[INFO] BatteryManager: Mode: CV -> Done (Vbatt=12605mV)
```
