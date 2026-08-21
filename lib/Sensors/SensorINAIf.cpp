#include "SensorINAIf.h"

bool SensorINAIf::isMeasurementValid() const
{
    if (!m_isConnected) return false;
    return (millis() - m_lastUpdateTime) < STALE_TIMEOUT_MS;
}

unsigned long SensorINAIf::measurementAge() const
{
    return millis() - m_lastUpdateTime;
}

bool SensorINAIf::isMeasurementUpdated()
{
    const unsigned long age = measurementAge();
    const bool updated = (age < m_lastMeasurementAge || m_lastMeasurementAge == 0);
    m_lastMeasurementAge = age; // always update so the next detection reliably sees a large previous value
    return updated;
}

bool SensorINAIf::isMeasurementSettled() const
{
    return true; // INA does not have a current settling time, so we assume it's always settled
}