# MPPT Algorithm

The firmware implements a **Perturb & Observe (P&O)** Maximum Power Point Tracking algorithm with variable step size, PI-based clamping, and soft ramp control.

---

## Overview

The Maximum Power Point (MPP) is the operating point on the solar panel's I-V curve where power output is maximised. The P&O algorithm finds this point by repeatedly nudging the converter's operating point and observing whether power went up or down.

```
P = V × I
              P (power)
               │       ← MPP
               │      /│\
               │     / │ \
               │    /  │  \
               │   /   │   \
               └───────────── V (voltage)
                         ↑
                     converter pushes voltage here
```

---

## Algorithm (MpptController)

### Constants

| Constant | Value | Meaning |
|---|---|---|
| `MIN_CONTROL_VALUE` | 0 | Minimum duty cycle / setpoint (0 %) |
| `MAX_CONTROL_VALUE` | 100 | Maximum duty cycle / setpoint (100 %) |
| `DEFAULT_STEP` | 1 | Starting step size |
| `MIN_STEP` | 1 | Floor for dynamic step |
| `MAX_STEP` | 5 | Ceiling for dynamic step |
| `MIN_DELTA_VOLTAGE_mV` | 10 | Noise threshold below which step is not updated |
| `K_STEP` | 2.5 | Proportionality constant for dynamic step scaling |

### Per-call logic (`MpptController::update()`)

```
1. Compute current power:  P  = V  × I  / 1000  (mW, avoids overflow)
2. Compute previous power: P₀ = V₀ × I₀ / 1000

3. ΔP = P  - P₀
4. ΔV = V  - V₀

5. If |ΔV| ≥ MIN_DELTA_VOLTAGE_mV (10 mV):
       step = clamp( K_STEP × |ΔP / ΔV|, MIN_STEP, MAX_STEP )
       if ΔP < 0:
           reverse direction   // moved away from MPP → go back

6. Apply step in current direction:
       control += step   (if direction == Up)
       control -= step   (if direction == Down)

7. Clamp control to [0, 100]
8. Save current measurements for next call
```

### Variable Step Size

The step is proportional to `|dP/dV|` — the slope of the power curve. Far from the MPP the curve is steep, so the step is large (fast convergence). Near the MPP the curve is flat, so the step is small (low steady-state oscillation).

---

## DPS MPPT Gating

With the DPS5005 (Modbus RTU), each read/write cycle takes **~50–100 ms**, but the main loop runs every **~3 ms** (≈ 15–30 iterations per Modbus cycle).

Without gating, the P&O algorithm would see `ΔP ≈ 0` on every stale read and randomly toggle direction, degrading tracking accuracy.

**Solution:** `ChargeController` only calls `MpptController::update()` when a genuinely new hardware reading has arrived:

```cpp
const unsigned long pvUpdateAge = m_pvMeasurements->lastTimeUpdated();
if (pvUpdateAge < m_lastPvUpdateAge || m_lastPvUpdateAge == 0)
{
    m_mpptController.update(pvMeasurements);  // new read arrived
}
m_lastPvUpdateAge = pvUpdateAge;
```

`lastTimeUpdated()` returns `millis() - lastSuccessfulReadTime`. If this value has decreased since our last call, a new read must have completed.

---

## PI Clamping (Voltage and Current Limits)

After MPPT produces a requested control value, `ChargeController` may further reduce it to enforce the battery profile limits. Two PI controllers operate independently:

### Current Clamp (CC and Precharge modes)

```
error = batteryCurrentMeasured - currentLimit
if error ≤ 0:
    integralError = 0
    no correction applied
else:
    integralError += error
    integralError = clamp(integralError, 0, MAX_INTEGRAL_ERROR=500)
    correction = Kp × error + Ki × integralError
    control = clamp(control - correction, 0, 100)
```

### Voltage Clamp (CV mode)

Identical structure to the current clamp, but uses `batteryVoltageMeasured` and `maxVoltage_mV`.

### PI Parameter Defaults

| Parameter | Value |
|---|---|
| `Kp` | 1.0 |
| `Ki` | 0.01 |
| `MAX_INTEGRAL_ERROR` | 500 |

See [Configuration](Configuration.md#libconfigh----charge-controller-configuration) for tuning guidance.

---

## Soft Ramp

Before writing to the actuator, the control value is soft-ramped to prevent sudden jumps:

```cpp
int softRampControl(int targetControl, int stepSize)
{
    if (targetControl > m_mpptControl)
        return min(targetControl, m_mpptControl + stepSize);
    else if (targetControl < m_mpptControl)
        return max(targetControl, m_mpptControl - stepSize);
    return targetControl;
}
```

The `stepSize` comes from `ActuatorIf::getMaxSoftStep()`:

| Actuator | `getMaxSoftStep()` | Rationale |
|---|---|---|
| `PwmDcConverter` | 5 | PWM responds in one loop tick; larger step is safe |
| `DPSxDcConverter` | 1 | Modbus cycle is ~100 ms; small step prevents overshoot |

---

## Stale Measurement Handling

If the PV measurement becomes stale (sensor disconnect, I²C error, Modbus timeout):

- **PV stale:** hold the last known control value; log a warning every 10 s.
- **Battery stale:** reduce control by 10 per loop tick (protective ramp-down); log every 10 s.

The battery state machine is only updated when measurements are valid to avoid tripping the `minSafeVoltage` Fault on a transient zero-read.

---

## Control Signal Flow

```
MPPT P&O  →  softRamp  →  PI current clamp  →  PI voltage clamp  →  actuator
(0-100)       (±step)      (CC mode only)       (CV mode only)       applyControl()
```

The MPPT runs first, then protection limits apply on top. This means the MPPT converges toward the solar MPP while the PI controllers ensure the battery never exceeds its rated limits.
