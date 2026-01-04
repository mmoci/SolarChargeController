#include "ChargeController.h"

void ChargeController::init()
{
    m_pvSensor.init();
    m_batterySensor.init();

    m_batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
    m_mpptController.init();
}

void ChargeController::update()
{
    int pwmDuty{};

    m_pvSensor.update();
    m_batterySensor.update();

    Measurements pvMeasurements{m_pvSensor.getVoltage_mV(), m_pvSensor.getCurrent_mA()};
    Measurements batteryMeasurements{m_batterySensor.getVoltage_mV(), m_batterySensor.getCurrent_mA()};

    handlePvPowerUnavailableTimer(pvMeasurements.voltage_mV * pvMeasurements.current_mA);

    m_mpptController.update(pvMeasurements);
    pwmDuty = m_mpptController.getRequestedPwmDuty();

    m_batteryManager.update(batteryMeasurements, isChargingAvailable());
    if(!m_batteryManager.isChargingAllowed())
    {
        m_dcConverter.setPwmDuty(0);
        return;
    }

    if(m_batteryManager.isCurrentLimitActive())
    {
        if(auto currentLimit = m_batteryManager.getMaxChargingCurrentLimit())
        {
            pwmDuty = clampCurrentLimit(batteryMeasurements.current_mA, *currentLimit, pwmDuty);
        }
    }

    if(m_batteryManager.isVoltageLimitActive())
    {
        if(auto voltageLimit = m_batteryManager.getMaxVoltageLimit())
        {
            pwmDuty = clampVoltageLimit(batteryMeasurements.voltage_mV, *voltageLimit, pwmDuty);
        }
    }

    m_dcConverter.setPwmDuty(pwmDuty);
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

int ChargeController::clampCurrentLimit(int batteryCurrent, int currentLimit, int pwmDuty)
{
    if (batteryCurrent <= 0) return pwmDuty;

    int deltaCurrent = batteryCurrent - currentLimit;
    
    if(deltaCurrent <= 0) return pwmDuty;

    int pwmCorrection = std::lround((static_cast<double>(pwmDuty) / batteryCurrent) * deltaCurrent);

    return constrain(pwmDuty - pwmCorrection, 0, DcConverterConfig::MAX_PWM_DUTY);
}

int ChargeController::clampVoltageLimit(int batteryVoltage, int voltageLimit, int pwmDuty)
{
    if (batteryVoltage <= 0) return pwmDuty;

    int deltaVoltage = batteryVoltage - voltageLimit;
    
    if(deltaVoltage <= 0) return pwmDuty;

    int pwmCorrection = std::lround((static_cast<double>(pwmDuty) / batteryVoltage) * deltaVoltage);
    
    return constrain(pwmDuty - pwmCorrection, 0, DcConverterConfig::MAX_PWM_DUTY);
}