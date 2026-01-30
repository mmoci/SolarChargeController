#include "ChargeController.h"

void ChargeController::init()
{
    m_batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
    m_mpptController.init();
}

void ChargeController::update()
{
    int pwmDuty{};

    Measurements pvMeasurements{m_pvMeasurements->getVoltage_mV(), m_pvMeasurements->getCurrent_mA()};
    Measurements batteryMeasurements{m_batteryMeasurements->getVoltage_mV(), m_batteryMeasurements->getCurrent_mA()};

    handlePvPowerUnavailableTimer(pvMeasurements.voltage_mV * pvMeasurements.current_mA);

    // In DPS mode, pvMeasurements represent output-side power
    m_mpptController.update(pvMeasurements);

    pwmDuty = m_mpptController.getRequestedPwmDuty();
    pwmDuty = std::min(pwmDuty, m_lastPwmDuty + ChargeControllerConfig::MAX_PWM_SOFT_STEP); // Prevents sudden jumps - PI soft recovery

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
            pwmDuty = clampLimitPI(batteryMeasurements.current_mA, *currentLimit, pwmDuty, m_currentIntegralError);
        }
    }

    if(m_batteryManager.isVoltageLimitActive())
    {
        if(auto voltageLimit = m_batteryManager.getMaxVoltageLimit())
        {
            pwmDuty = clampLimitPI(batteryMeasurements.voltage_mV, *voltageLimit, pwmDuty, m_voltageIntegralError);
        }
    }

    m_lastPwmDuty = pwmDuty;
    m_actuator->applyControl(pwmDuty);
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

int ChargeController::clampLimit(int measured, int limit, int pwmDuty)
{
    if (measured <= 0) return pwmDuty;

    int deltaCurrent = measured - limit;
    
    if(deltaCurrent <= 0) return pwmDuty;

    int pwmCorrection = std::lround((static_cast<double>(pwmDuty) / measured) * deltaCurrent);

    return constrain(pwmDuty - pwmCorrection, 0, DcConverterConfig::MAX_PWM_DUTY);
}

int ChargeController::clampLimitPI(int measured, int limit, int pwmDuty, long& integralError)
{
    if (measured <= 0) return pwmDuty;

    int error = measured - limit;
    
    if(error <= 0) 
    {
        integralError = 0;
        return pwmDuty;
    }

    // Used PI (Proportional & Integral) method for compensation as gives better results than linear approach 
    integralError += error;
    integralError = constrain(integralError, 0L, ChargeControllerConfig::MAX_INTEGRAL_ERROR);
    int pwmCorrection = static_cast<int>(std::roundl(ChargeControllerConfig::Kp * error + ChargeControllerConfig::Ki * integralError));

    return constrain(pwmDuty - pwmCorrection, 0, DcConverterConfig::MAX_PWM_DUTY);
}