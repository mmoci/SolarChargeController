#pragma once
#include <memory>
#include "MeasurementsIf.h" 
#include "ActuatorIf.h"
#include "MpptController.h"
#include "BatteryManager.h"
#include "Config.h"

class ChargeController
{
    public:
    ChargeController(MeasurementsIf* pvMeasurements, MeasurementsIf* batteryMeasurements, ActuatorIf* actuator) :
        m_pvMeasurements{pvMeasurements},
        m_batteryMeasurements{batteryMeasurements},
        m_actuator{actuator}
    {}

    void init();
    void update();

    private:
    MeasurementsIf* m_pvMeasurements{};
    MeasurementsIf* m_batteryMeasurements{};
    ActuatorIf*     m_actuator{};
    BatteryManager m_batteryManager{};
    MpptController m_mpptController{};

    // Used to determine is charging available (PV power)
    long m_pvPower_mW{};
    Timer m_pvPowerUnavailableTimer{};

    // PI (Proportional & Integral) variables containing cumulative integral error
    long m_voltageIntegralError{};
    long m_currentIntegralError{};

    // Last value for PWM duty cycle
    int m_mpptControl{};

    bool isChargingAvailable();
    void handlePvPowerUnavailableTimer(long pvPower_mW);
    int clampLimit(int measured, int limit, int mpptControl);
    int clampLimitPI(int measured, int limit, int mpptControl, long& integralError);
};