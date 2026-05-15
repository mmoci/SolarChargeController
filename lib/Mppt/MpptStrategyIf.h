#pragma once

#include "Utility.h"

class MpptStrategyIf
{
    public:
    virtual void init() = 0;
    virtual void update(Measurements pvMeasurements) = 0;
    virtual int getMpptControl() const = 0; // 0-100% control signal to apply to the actuator
    virtual void setOpenCircuitVoltage(int /*voc_mV*/) {} // Only relevant for strategies that use Voc to inform their algorithm (e.g. input voltage regulation); provided as a hook since ChargeController calls it for all strategies
    virtual int getMaxSoftRampStep() const { return MAX_STEP; } // Maximum allowed change in control output per update cycle when soft ramping is applied; override to tune per-strategy

    virtual ~MpptStrategyIf() = default;

    static constexpr int MAX_STEP{10};     // Default soft-ramp step size (% of control range per update cycle)
    static constexpr int DEFAULT_STEP{1};  // Default P&O perturbation step
    static constexpr int MIN_STEP{1};      // Minimum P&O perturbation step
    static constexpr int MIN_CONTROL_VALUE{0};
    static constexpr int MAX_CONTROL_VALUE{100};
};