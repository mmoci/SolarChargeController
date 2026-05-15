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
    int getMaxSoftRampStep() const override { return MAX_STEP; } // Conservative ramp to prevent overshooting panel Isc at low irradiance during re-enable after OCV capture

    private:
    int m_control{};
    int m_openCircuitVoltage_mV{ PvArrayConfig::DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV };
    int m_mpptVoltage{};
    Measurements m_pvMeasurements{};

    // Conservative ramp step: 2% × 3A = 60mA/cycle. Prevents overshooting panel Isc at low irradiance during re-enable after OCV capture.
    static constexpr int MAX_STEP{2};

    // Proportional gain: % control output per mV of Vin error.
    // Loop stability: Δcontrol[%] = K_P × error[mV] — increasing I_SET draws more panel current, pulling Vin down — negative feedback, inherently stable.
    static constexpr float K_P{0.005f};
};