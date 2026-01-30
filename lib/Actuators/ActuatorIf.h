#pragma once

class ActuatorIf
{
public:
    // Apply a control value decided by ChargeController
    virtual void applyControl(int controlValue) = 0;

    // Optional: bounds of valid control range - we'll see about this
    //virtual int minControl() const = 0;
    //virtual int maxControl() const = 0;

    virtual ~ActuatorIf() = default;
};