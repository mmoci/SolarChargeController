#pragma once
#include "MeasurementsIf.h"

class MockMeasurements : public MeasurementsIf
{
public:
    MockMeasurements() = default;
    
    int getVoltage_mV() const override { return m_voltage_mV; }
    int getCurrent_mA() const override { return m_current_mA; }
    bool isMeasurementValid() const override { return m_valid; }

    bool isMeasurementUpdated() override
    {
        // m_lastUpdate is used as the age (ms since last read) in tests — same wrap
        // detection as the real DPS/INA226 implementations.
        const bool updated = (m_lastUpdate < m_lastMeasurementAge || m_lastMeasurementAge == 0);
        m_lastMeasurementAge = m_lastUpdate;
        return updated;
    }

    // Setters for testing
    void setVoltage_mV(int voltage) { m_voltage_mV = voltage; }
    void setCurrent_mA(int current) { m_current_mA = current; }
    void setValid(bool valid) { m_valid = valid; }
    void setLastUpdate(unsigned long time) { m_lastUpdate = time; }

private:
    int m_voltage_mV = 0;
    int m_current_mA = 0;
    bool m_valid = true;
    unsigned long m_lastUpdate = 0;
    unsigned long m_lastMeasurementAge = 0;
};
