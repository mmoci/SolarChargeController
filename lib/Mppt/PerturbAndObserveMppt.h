#pragma once

#include "MpptStrategyIf.h"

class PerturbAndObserveMppt : public MpptStrategyIf
{
    public:
    struct MpptGradient
    {
        float gradient{};
        bool valid{false};
    };  

    void init() override;
    void update(Measurements pvMeasurements) override;
    int getMpptControl() const override {return m_control;}
    void setOpenCircuitVoltage(int openCircuitVoltage_mV) override {} // No-op for P&O, but need to override since it's called by ChargeController
    int getMaxSoftRampStep() const override {return MAX_STEP;} // Default ramping; can be overridden by specific strategies if needed
    int getMpptStep() const {return m_step;}
    void setMpptStep(int step) {m_step = step;}

    private:
    enum class Direction
    {
        Down,
        Up
    };

    // Minimum voltage change to trigger a step change, prevents overreacting to noise
    static constexpr int MIN_DELTA_VOLTAGE_mV{10};
    
    // Proportionality constant for dynamic step sizing (units of control output per (mW/mV)), 
    // need to be tuned based on typical |dP/dV| range and desired responsiveness
    static constexpr float K_STEP{2.5f};

    // Limits for dynamic step sizing based on gradient magnitude, to prevent excessive steps in very steep regions or when noise creates a large apparent gradient
    static constexpr int DEFAULT_STEP{1};
    static constexpr int MIN_STEP{1};
    static constexpr int MAX_STEP{10}; // Keep overshoot small near the I-V knee; dynamic step handles fast climb when far from MPP

    // Previous measurements for gradient calculation and dynamic step sizing
    Measurements m_pvMeasurements{};
    int m_control{MIN_CONTROL_VALUE};
    MpptGradient m_gradientData{};
    Direction m_direction{};
    int m_step{DEFAULT_STEP};
};