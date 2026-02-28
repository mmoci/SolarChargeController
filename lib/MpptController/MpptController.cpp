#include "MpptController.h"

void MpptController::init()
{
    m_outputData.control = MIN_CONTROL_VALUE;
    m_step = DEFAULT_STEP;
    m_direction = Direction::Up;
}

void MpptController::update(Measurements pvMeasurements)
{
    auto [voltage_mV, current_mA] = pvMeasurements;
    auto [m_voltage_mV, m_current_mA] = m_pvMeasurements;

    long power_mW {voltage_mV * current_mA}; 
    long m_power_mW {m_voltage_mV * m_current_mA};

    if(power_mW < m_power_mW)
        m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up; 

    if(m_direction == Direction::Up)
        m_outputData.control += m_step;
    else
        m_outputData.control -= m_step;

    m_outputData.control = constrain(m_outputData.control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);
    m_pvMeasurements = pvMeasurements;
}