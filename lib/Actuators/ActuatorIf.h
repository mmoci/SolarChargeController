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

    // For actuators that can enable/disable output
    virtual void enableOutput(bool enable, bool priority = false) = 0;

    // Apply a control value decided by ChargeController
    virtual void applyControl(int controlValue) = 0;

    // Called when the battery profile is set or changed. Actuators that use the
    // battery profile (e.g. DPS OVP ceiling) should override this; default is no-op.
    virtual void setBatteryProfile(const BatteryProfile& /*profile*/) {}

    // Returns true when measurements reflect the currently applied setpoint.
    // Actuators with write-to-read lag (e.g. Modbus) should override this to
    // prevent P&O from computing gradients against a stale pre-write reading.
    virtual bool areMeasurementsSettled() const { return true; }

    // Actuator capabilities
    virtual ControlMode getControlMode() const = 0;
    virtual int getMinControl() const = 0;
    virtual int getMaxControl() const = 0;
    virtual bool hasMeasurements() const = 0;
    virtual int  getMaxSoftStep()  const = 0;

    virtual ~ActuatorIf() = default;
};