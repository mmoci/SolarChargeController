#pragma once
#include "ActuatorIf.h"

class MockActuator : public ActuatorIf
{
public:
    MockActuator(ControlMode mode = ControlMode::DUTY_CYCLE) : m_mode(mode) {}
    
    void applyControl(int controlValue) override 
    { 
        m_lastControl = controlValue; 
    }

    void enableOutput(bool enable, bool /*priority*/ = false) override
    {
        m_outputEnabled = enable;
    }

    ControlMode getControlMode() const override { return m_mode; }
    int getMinControl() const override { return 0; }
    int getMaxControl() const override { return 100; }
    bool hasMeasurements() const override { return true; }

    // Getters for testing
    int getLastControl() const { return m_lastControl; }
    void resetLastControl() { m_lastControl = -1; }
    bool isOutputEnabled() const override { return m_outputEnabled; }

private:
    int m_lastControl = -1;
    bool m_outputEnabled = false;
    ControlMode m_mode;
};
