#include "MpptController.h"
#include "DcConverter.h"
#include "Config.h"

void MpptController::init()
{
    m_pwmDuty = DcConverterConfig::DEFAULT_PWM_DUTY;
    m_step = DEFAULT_PWM_STEP;
    m_direction = Direction::Up;
}

void MpptController::update(const Measurements& pvMeasurements)
{
    auto& [pvVoltage, pvCurrent] = pvMeasurements;

    int power {pvVoltage * pvCurrent};
    int previousPower {m_pvMeasurements.voltage_mV * m_pvMeasurements.current_mA};

    if(power < previousPower)
        m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up; 

    if(m_direction == Direction::Up)
        m_pwmDuty += m_step;
    else
        m_pwmDuty -= m_step;

    m_pwmDuty = constrain(m_pwmDuty, 0, DcConverterConfig::MAX_PWM_DUTY);
    m_pvMeasurements = pvMeasurements;
}