#pragma once

#include "MpptStrategyIf.h"
#include "Config.h"

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
    void setOpenCircuitVoltage(int openCircuitVoltage_mV) override;
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

    // Voltage change threshold for knee detection, used to prevent overshooting the MPP knee
    static constexpr int KNEE_DELTA_VOLTAGE_THRESHOLD_mV{150}; 
    
    // Proportionality constant for dynamic step sizing (units of control output per (mW/mV)), 
    // need to be tuned based on typical |dP/dV| range and desired responsiveness
    static constexpr float K_STEP{0.5f}; // [mW/mV]

    // Minimum Vin increase above the pre-collapse operating point that indicates irradiance has increased
    // enough to invalidate the collapse ceiling. Sized to reject measurement noise (~100mV) while
    // reliably detecting real irradiance changes (typically >500mV shift at same I_SET).
    static constexpr int IRRADIANCE_INCREASE_VOLTAGE_MARGIN_mV{500};

    // Previous measurements for gradient calculation and dynamic step sizing
    Measurements m_pvMeasurements{};
    int m_control{MIN_CONTROL_VALUE};
    int m_controlCollapseCeiling{MIN_CONTROL_VALUE};
    int m_voltageAtCeiling_mV{};  // Vin just before the last collapse; used to detect irradiance increase
    CircularBuffer<std::pair<int, int>, 3> m_collapsePointsCandidates{}; // Queue of recent (control, voltage) points that are candidates for collapse points
    MpptGradient m_gradientData{};
    Direction m_direction{};
    int m_step{DEFAULT_STEP};
    int m_openCircuitVoltage_mV{};
};