#pragma once
#include "BatteryProfile.h"

class ActuatorIf
{
public:
    enum class ControlMode
    {
        DUTY_CYCLE,
        VOLTAGE_SETPOINT,
        CURRENT_SETPOINT
    };

    virtual void enableOutput(bool enable, bool priority = false) = 0;
    virtual bool isOutputEnabled() const = 0;
    virtual void applyControl(int controlValue) = 0;
    virtual void setBatteryProfile(const BatteryProfile& /*profile*/) {}

    // Returns true when measurements reflect the currently applied setpoint.
    // Actuators with write-to-read lag (e.g. Modbus) should override this to
    // prevent P&O from computing gradients against a stale pre-write reading.
    virtual bool areMeasurementsSettled() const { return true; }
    virtual ControlMode getControlMode() const = 0;
    virtual int getMinControl() const = 0;
    virtual int getMaxControl() const = 0;
    virtual bool hasMeasurements() const = 0;

    virtual ~ActuatorIf() = default;
};