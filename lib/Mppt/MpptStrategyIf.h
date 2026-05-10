#pragma once

#include "Utility.h"

class MpptStrategyIf
{
    public:
    static constexpr int DEFAULT_STEP{1};
    static constexpr int MIN_CONTROL_VALUE{0};
    static constexpr int MAX_CONTROL_VALUE{100};
    static constexpr int MIN_STEP{1};
    static constexpr int MAX_STEP{10}; // Keep overshoot small near the I-V knee; dynamic step handles fast climb when far from MPP

    virtual void init() = 0;
    virtual void update(Measurements pvMeasurements) = 0;
    virtual int getMpptControl() const = 0; // 0-100% control signal to apply to the actuator
    virtual void setOpenCircuitVoltage(int /*voc_mV*/) {} // Only relevant for strategies that use Voc to inform their algorithm (e.g. input voltage regulation); provided as a hook since ChargeController calls it for all strategies
    virtual int getMaxSoftRampStep() const { return MAX_STEP; } // Maximum allowed change in control per update cycle for smooth ramping; default is 10% per cycle, can be overridden by specific strategies if needed

    virtual ~MpptStrategyIf() = default;
};