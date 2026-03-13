#pragma once
#include "MeasurementsIf.h"

class MockMeasurements : public MeasurementsIf
{
public:
    MockMeasurements() = default;
    
    int getVoltage_mV() const override { return m_voltage_mV; }
    int getCurrent_mA() const override { return m_current_mA; }
    bool isMeasurementValid() const override { return m_valid; }
    unsigned long lastTimeUpdated() const override { return m_lastUpdate; }

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
};
