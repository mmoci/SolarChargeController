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
    static constexpr int MAX_STEP{10}; // Keep overshoot small near the I-V knee; dynamic step handles fast climb when far from MPP

    // Voltage drop threshold for collapse detection. A genuine panel cliff collapse drops Vin by
    // 20–30 V in a single P&O step (e.g. 41 V → 12.75 V = Vbatt). A normal P&O overshoot near
    // MPP drops Vin by at most 1–2 V. Gradient-based detection is unreliable because both
    // scenarios produce similar |dP/dV| values (1–4 mW/mV); voltage drop is unambiguous.
    // Tune: raise if step-induced voltage dips > 5 V on your panel; lower not recommended.
    static constexpr int           COLLAPSE_VOLTAGE_DROP_THRESHOLD_mV = 5000;   // 5 V drop in one P&O step → collapse
    static constexpr unsigned long COLLAPSE_DURATION                  = 5000UL; // Stabilization window (ms): P&O suppressed while DPS recovers after collapse
    static constexpr int           BACKOFF_COLLAPSE_FACTOR            = 90;     // Back off to 90% of the pre-collapse setpoint on detection
    static constexpr unsigned long CEILING_RELAX_INTERVAL_MS          = 5000UL; // Raise ceiling by 1% every 5s so MPPT can adapt to improving irradiance
    static constexpr int           CEILING_RELAX_STEP                 = 1;      // Amount (in % control) to raise ceiling per interval

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
    int m_step{DEFAULT_STEP};

    // Variables for PV collapse detection and recovery
    MpptOutputData m_collapseLimitData{MAX_CONTROL_VALUE}; // Ceiling starts at 100% (no restriction)
    Timer m_collapsedTimer{};
    bool m_isCollapsing{false};
    unsigned long m_lastCeilingRelaxTime{0};
};