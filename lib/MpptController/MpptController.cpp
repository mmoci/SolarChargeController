#include "MpptController.h"

void MpptController::init()
{
    m_outputData.control = MIN_CONTROL_VALUE;
    m_step = DEFAULT_STEP;
    m_direction = Direction::Up;
}

void MpptController::update(Measurements pvMeasurements)
{
    long power_mW {pvMeasurements.voltage_mV * pvMeasurements.current_mA / 1000}; // Convert to mW to avoid overflow and match typical PV power units
    long m_power_mW {m_pvMeasurements.voltage_mV * m_pvMeasurements.current_mA / 1000}; // Previous power in mW

    long deltaPower_mW = power_mW - m_power_mW;
    long deltaVoltage_mV = pvMeasurements.voltage_mV - m_pvMeasurements.voltage_mV;

    if(std::abs(deltaVoltage_mV) >= MIN_DELTA_VOLTAGE_mV)
    {
        // Scale step proportionally to |dP/dV|, clamped to [MIN_STEP, MAX_STEP]
       m_step = constrain(static_cast<int>(K_STEP * std::abs(static_cast<float>(deltaPower_mW) / static_cast<float>(deltaVoltage_mV))), MIN_STEP, MAX_STEP
        );

        // Debug output to observe dynamic step sizing behavior
        Serial.printf("[MPPT] |dP/dV|=%.3f A, step=%d\n", std::abs(static_cast<float>(deltaPower_mW) / static_cast<float>(deltaVoltage_mV)), m_step);
        
        if(deltaPower_mW < 0)
            m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up; 
    }

    if(m_direction == Direction::Up)
        m_outputData.control += m_step;
    else
        m_outputData.control -= m_step;

    m_outputData.control = constrain(m_outputData.control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);
    m_pvMeasurements = pvMeasurements;
}