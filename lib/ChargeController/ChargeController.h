#pragma once
#include <memory>
#include "MeasurementsIf.h" 
#include "ActuatorIf.h"
#include "MpptStrategyIf.h"
#include "BatteryManager.h"
#include "BatteryProfileSelector.h"
#include "Config.h"

class ChargeController
{
    public:
    ChargeController(MeasurementsIf* pvMeasurements, MeasurementsIf* batteryMeasurements, ActuatorIf* actuator, MpptStrategyIf* mpptStrategy, BatteryProfileSelector* profileSelector = nullptr) :
        m_pvMeasurements{pvMeasurements},
        m_batteryMeasurements{batteryMeasurements},
        m_actuator{actuator},
        m_mpptStrategy{mpptStrategy},
        m_profileSelector{profileSelector}
    {}

    void init();
    void update();

    BatteryManager::Mode getBatteryMode() const { return m_batteryManager.getMode(); }
    int getMpptControl() const { return m_mpptControl; }

    private:
    // Pointers to hardware interfaces, injected via constructor for flexibility and testability
    MeasurementsIf*          m_pvMeasurements{};
    MeasurementsIf*          m_batteryMeasurements{};
    ActuatorIf*              m_actuator{};
    BatteryProfileSelector*  m_profileSelector{};
    MpptStrategyIf*          m_mpptStrategy{};
    BatteryManager           m_batteryManager{};

    struct MeasurementSnapshot
    {
        Measurements pv{};
        Measurements battery{};
        int pvOpenCircuitVoltage_mV{};
        bool pvValid{false};      // PV measurement is within stale timeout
        bool batteryValid{false}; // Battery measurement is within stale timeout
        bool updated{false};      // New PV reading arrived this cycle (implies pvValid)
    };

    // Used to determine if charging is available (PV input vs battery voltage)
    bool m_isPvAvailable{false};

    // PI (Proportional & Integral) variables containing cumulative integral error
    long m_voltageIntegralError{};
    long m_currentIntegralError{};

    // Last value for PWM duty cycle
    int m_mpptControl{};

    // Tracks whether charging was allowed on the previous update cycle.
    // Used to detect the Idle→CC transition and reset MPPT to a clean state,
    // and to gate MPPT updates so the algorithm only runs while actively charging.
    bool m_wasChargingAllowed{false};

    // Tracks whether the CV voltage limit was active on the previous update cycle.
    // Used to detect the CV→CC transition so MPPT can be reset after the voltage
    // limit releases, preventing it from re-using stale gradient data from when
    // the panel was unloaded.
    bool m_wasVoltageLimitActive{false};

    // Tracks the settled output-enabled state from the previous cycle.
    // Used to detect OFF→ON transitions (external re-enable or OCV recovery) so MPPT
    // can be reset before the first update, preventing stale high I_SET from before
    // the output was off from being applied immediately on re-enable.
    bool m_wasOutputEnabled{false};

    bool updatePvAvailability(Measurements pvMeasurements, Measurements batteryMeasurements);
    int clampLimitPI(int measured, int limit, int mpptControl, long& integralError);
    int softRampControl(int targetControl, int stepSize);
    void handleBatteryChargingStates(bool chargingAllowed, bool isVoltageLimitActive);
    MeasurementSnapshot sampleMeasurements();
    int computeDesiredSetpoint(MeasurementSnapshot snapshot);
    int applyLimitConstraints(int desiredSetpoint, MeasurementSnapshot snapshot);
    void resetMpptStrategy();
};