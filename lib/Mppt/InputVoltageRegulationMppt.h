#pragma once

#include "MpptStrategyIf.h"
#include "Config.h"

class InputVoltageRegulationMppt : public MpptStrategyIf
{
    public:
    void init() override;
    void update(Measurements pvMeasurements) override;
    int getMpptControl() const override {return m_control;}
    void setOpenCircuitVoltage(int openCircuitVoltage_mV) override;
    int getMaxSoftRampStep() const override { return MAX_CONTROL_VALUE; } // More aggressive ramping than default to respond faster to changing conditions, since this strategy is less prone to instability than Perturb & Observe
    int getMpptStep() const {return m_step;}
    void setMpptStep(int step) {m_step = step;}

    private:
    int m_control{};
    int m_step{};
    int m_openCircuitVoltage_mV{ PvArrayConfig::DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV };
    int m_mpptVoltage{};
    Measurements m_pvMeasurements{};

    // MPPT voltage as a percentage of open-circuit voltage, based on typical PV characteristics
    static constexpr int INITIAL_MPPT_VOLTAGE_PERCENT{76};

    // Proportional gain: % control output per mV of Vin error.
    // Loop stability: Δcontrol[%] = K_P × error[mV] — increasing I_SET draws more panel current, pulling Vin down — negative feedback, inherently stable.
    static constexpr float K_P{0.005f};
};