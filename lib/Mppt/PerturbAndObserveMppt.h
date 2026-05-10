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

    // Minimum voltage change to trigger a step change, prevents overreacting to noise
    static constexpr int MIN_DELTA_VOLTAGE_mV{10};
    
    // Proportionality constant for dynamic step sizing (units of control output per (mW/mV)), 
    // need to be tuned based on typical |dP/dV| range and desired responsiveness
    static constexpr float K_STEP{2.5f};  

    void init() override;
    void update(Measurements pvMeasurements) override;
    int getMpptControl() const override {return m_control;}
    void setOpenCircuitVoltage(int openCircuitVoltage_mV) override {} // No-op for P&O, but need to override since it's called by ChargeController
    int getMpptStep() const {return m_step;}
    void setMpptStep(int step) {m_step = step;}

    private:
    enum class Direction
    {
        Down,
        Up
    };

    // Previous measurements for gradient calculation and dynamic step sizing
    Measurements m_pvMeasurements{};
    int m_control{MIN_CONTROL_VALUE};
    MpptGradient m_gradientData{};
    Direction m_direction{};
    int m_step{DEFAULT_STEP};
};