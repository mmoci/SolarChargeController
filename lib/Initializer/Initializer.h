#pragma once

#include "MeasurementsIf.h"
#include "ActuatorIf.h"
#include "MpptStrategyIf.h"

class Initializer
{
    public:
    static Initializer& getInstance();
    virtual void init() = 0;
    virtual void update() = 0;
    virtual MeasurementsIf& getPvMeasurements() = 0;
    virtual MeasurementsIf& getBatteryMeasurements() = 0;
    virtual MpptStrategyIf& getMpptStrategy() = 0;
    virtual ActuatorIf& getActuator() = 0;
    virtual ~Initializer() = default;

    protected:
    Initializer() = default; // Protected constructor for singleton pattern
    Initializer(const Initializer&) = delete; // Delete copy constructor
    Initializer(Initializer&&) = delete; // Delete move constructor
    Initializer& operator=(const Initializer&) = delete; // Delete copy assignment operator
    Initializer& operator=(Initializer&&) = delete; // Delete move assignment operator
};