#pragma once

#include "Utility.h"

// TODO: Config namespace with static variables

class MpptController
{
    public:
    struct MpptOutputData
    {
        int control{};
    };

    struct MpptGradientData
    {
        float gradient{};
        bool valid{false};
    };

    static constexpr int DEFAULT_STEP{1};
    static constexpr int MIN_CONTROL_VALUE{0};
    static constexpr int MAX_CONTROL_VALUE{100};
    static constexpr int MIN_STEP{1};
    static constexpr int MAX_STEP{5};

    // A large |dP/dV| with ΔP<0 means voltage crashed in a single P&O step — the sharp-knee cliff.
    // Normal P&O near MPP produces small gradients (~0.1–2 mW/mV). Start at 5.0f and tune:
    // lower if genuine collapses are missed, higher if normal P&O steps trigger false alarms.
    static constexpr float   COLLAPSE_GRADIENT_THRESHOLD_mW_mV = 5.0f;
    static constexpr unsigned long COLLAPSE_DURATION           = 5000UL; // Stabilization window (ms): P&O suppressed while DPS recovers after collapse
    static constexpr int     BACKOFF_COLLAPSE_FACTOR           = 70;    // Back off to 70% of the pre-collapse setpoint on detection
    static constexpr unsigned long CEILING_RELAX_INTERVAL_MS   = 30000UL; // Raise ceiling by 1% every 30 s so MPPT can adapt to improving irradiance
    static constexpr int     CEILING_RELAX_STEP                = 1;     // Amount (in % control) to raise ceiling per interval

    // Minimum voltage change to trigger a step change, prevents overreacting to noise
    static constexpr int MIN_DELTA_VOLTAGE_mV{10};
    
    // Proportionality constant for dynamic step sizing (units of control output per (mW/mV)), 
    // need to be tuned based on typical |dP/dV| range and desired responsiveness
    static constexpr float K_STEP{2.5f};  

    void init();
    void update(Measurements mpptInputData);
    int getRequestedOutput() const {return m_outputData.control;}
    int getOutputStep() const {return m_step;}
    void setOutputStep(int step) {m_step = step;}
    bool isPvPowerCollapsing() const { return m_isCollapsing; }

    private:
    enum class Direction
    {
        Down,
        Up
    };

    // Previous measurements for gradient calculation and dynamic step sizing
    Measurements m_pvMeasurements{};
    MpptOutputData m_outputData{};
    MpptGradientData m_gradientData{};
    Direction m_direction{};
    int m_step{};

    // Variables for PV collapse detection and recovery
    MpptOutputData m_collapseLimitData{MAX_CONTROL_VALUE}; // Ceiling starts at 100% (no restriction)
    Timer m_collapsedTimer{};
    bool m_isCollapsing{false};
    unsigned long m_lastCeilingRelaxTime{0};
};