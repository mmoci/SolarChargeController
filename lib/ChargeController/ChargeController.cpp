#include "ChargeController.h"
#include "DPSxDcConverter.h"
#include "PwmDcConverter.h"

void ChargeController::init(const BatteryProfile& batteryProfile)
{
    m_batteryManager.init(batteryProfile);
    m_mpptController.init();
}

void ChargeController::update()
{
    int mpptControl{};

    Measurements pvMeasurements{m_pvMeasurements->getVoltage_mV(), m_pvMeasurements->getCurrent_mA()};
    Measurements batteryMeasurements{m_batteryMeasurements->getVoltage_mV(), m_batteryMeasurements->getCurrent_mA()};

    if(!m_pvMeasurements->isMeasurementValid())
    {
        Serial.println("[ChargeController] WARNING: PV measurements are stale!");
        mpptControl = m_mpptControl;  // Maintain last known control to prevent sudden jumps when measurements are stale, but don't reset to 0 to allow soft recovery when PV measurements become valid again
    }
    else if(!m_batteryMeasurements->isMeasurementValid())
    {
        Serial.println("[ChargeController] WARNING: Battery measurements are stale!");
        mpptControl = std::max(0, m_mpptControl - 10);  // Reduce charging current to prevent potential overcharging when battery measurements are unavailable
    }
    else
    {
        handlePvPowerUnavailableTimer(pvMeasurements.voltage_mV * pvMeasurements.current_mA);

        m_mpptController.update(pvMeasurements);

        mpptControl = m_mpptController.getRequestedOutput();

        // Implements a soft ramping mechanism for the MPPT control value. The soft ramping is applied on every update to ensure smooth transitions in control output.
        mpptControl = softRampControl(mpptControl, ChargeControllerConfig::MAX_CONTROL_SOFT_STEP);
    }

    m_batteryManager.update(batteryMeasurements, isChargingAvailable());

    if(!m_batteryManager.isChargingAllowed())
    {
        m_actuator->applyControl(0);
        return;
    }

    if(m_batteryManager.isCurrentLimitActive())
    {
        if(auto currentLimit = m_batteryManager.getMaxChargingCurrentLimit())
        {
            mpptControl = clampLimitPI(batteryMeasurements.current_mA, *currentLimit, mpptControl, m_currentIntegralError);
        }
    }

    if(m_batteryManager.isVoltageLimitActive())
    {
        if(auto voltageLimit = m_batteryManager.getMaxVoltageLimit())
        {
            mpptControl = clampLimitPI(batteryMeasurements.voltage_mV, *voltageLimit, mpptControl, m_voltageIntegralError);
        }
    }

    m_mpptControl = mpptControl;
    m_actuator->applyControl(mpptControl);
}

bool ChargeController::isChargingAvailable()
{
    return m_pvPowerUnavailableTimer.getDuration() < ChargeControllerConfig::PV_POWER_UNAVAILABLE_TIMEOUT;
}

void ChargeController::handlePvPowerUnavailableTimer(long pvPower_mW)
{
    if(pvPower_mW <= ChargeControllerConfig::PV_POWER_THRESHOLD)
    {
        if(m_pvPower_mW > ChargeControllerConfig::PV_POWER_THRESHOLD) 
            m_pvPowerUnavailableTimer.trigger();
        else
            m_pvPowerUnavailableTimer.update();
    }
    else
        m_pvPowerUnavailableTimer.reset();

    m_pvPower_mW = pvPower_mW;
}

int ChargeController::clampLimit(int measured, int limit, int mpptControl)
{
    if (measured <= 0) return mpptControl;

    int deltaCurrent = measured - limit;
    
    if(deltaCurrent <= 0) return mpptControl;

    int mpptControlCorrection = std::lround((static_cast<double>(mpptControl) / measured) * deltaCurrent);

    return constrain(mpptControl - mpptControlCorrection, 0, 100);
}

int ChargeController::clampLimitPI(int measured, int limit, int mpptControl, long& integralError)
{
    if (measured <= 0) 
        return mpptControl;

    int error = measured - limit;
    
    if(error <= 0) 
    {
        integralError = 0;
        return mpptControl;
    }

    // Used PI (Proportional & Integral) method for compensation as gives better results than linear approach 
    integralError += error;
    integralError = constrain(integralError, 0L, ChargeControllerConfig::MAX_INTEGRAL_ERROR);
    int mpptControlCorrection = static_cast<int>(std::roundl(ChargeControllerConfig::Kp * error + ChargeControllerConfig::Ki * integralError));

    return constrain(mpptControl - mpptControlCorrection, 0, 100);
}

int ChargeController::softRampControl(int targetControl, int stepSize)
{
    if(targetControl > m_mpptControl)
            return std::min(targetControl, m_mpptControl + stepSize);
    else if(targetControl < m_mpptControl)
            return std::max(targetControl, m_mpptControl - stepSize);
    return targetControl;
}