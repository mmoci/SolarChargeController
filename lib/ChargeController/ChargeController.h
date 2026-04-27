#pragma once
#include <memory>
#include "MeasurementsIf.h" 
#include "ActuatorIf.h"
#include "MpptController.h"
#include "BatteryManager.h"
#include "BatteryProfileSelector.h"
#include "Config.h"

class ChargeController
{
    public:
    ChargeController(MeasurementsIf* pvMeasurements, MeasurementsIf* batteryMeasurements, ActuatorIf* actuator, BatteryProfileSelector* profileSelector = nullptr) :
        m_pvMeasurements{pvMeasurements},
        m_batteryMeasurements{batteryMeasurements},
        m_actuator{actuator},
        m_profileSelector{profileSelector}
    {}

    void init();
    void update();

    BatteryManager::Mode getBatteryMode() const { return m_batteryManager.getMode(); }
    int getMpptControl() const { return m_mpptControl; }

    private:
    MeasurementsIf*          m_pvMeasurements{};
    MeasurementsIf*          m_batteryMeasurements{};
    ActuatorIf*              m_actuator{};
    BatteryProfileSelector*  m_profileSelector{};
    BatteryManager           m_batteryManager{};
    MpptController           m_mpptController{};
    

    // Used to determine if charging is available (PV input vs battery voltage)
    long m_pvPower_mW{};
    bool m_pvAvailable{false}; // true when Vin > Vbatt+headroom (converter can physically conduct)

    // Tracks the last reported PV measurement age so MPPT is only perturbed
    // when a genuinely new hardware reading has arrived (relevant for DPS where
    // Modbus takes ~100ms but the control loop runs every ~6ms).
    unsigned long m_lastPvUpdateAge{0};

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

    bool isChargingAvailable();
    void updatePvAvailability(long pvPower_mW, int pvVoltage_mV, int battVoltage_mV);
    int clampLimit(int measured, int limit, int mpptControl);
    int clampLimitPI(int measured, int limit, int mpptControl, long& integralError);
    int softRampControl(int targetControl, int stepSize);
};