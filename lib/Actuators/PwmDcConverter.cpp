#include "PwmDcConverter.h"
#include "Arduino.h"

PwmDcConverter::PwmDcConverter(uint8_t pin, int ledChannel, int frequency, int resolution) : 
    m_pin{pin},
    m_ledChannel{ledChannel},
    m_frequency{frequency},
    m_resolution{resolution},
    m_pwm{0}
{}

void PwmDcConverter::init()
{
    // Configure PWM functionality
    ledcSetup(m_ledChannel, m_frequency, m_resolution);

    // Attach the channel to the GPIO to be controlled
    ledcAttachPin(m_pin, m_ledChannel);

    #ifdef ESP32
    ledcWrite(m_ledChannel, 0); // For safty reason
    #elif defined(ESP8266)
    analogWrite(m_pwmPin, 0); // For safty reason
    #endif
}

void PwmDcConverter::update()
{}

ActuatorIf::ControlMode PwmDcConverter::getControlMode() const
{
    return m_controlMode;
}

int PwmDcConverter::getMinControl() const
{
    return 0;
}

int PwmDcConverter::getMaxControl() const
{
    return PwmDcConverterConfig::MAX_MPPT_CONTROL_VALUE;
}

bool PwmDcConverter::hasMeasurements() const
{
    return false;
}

void PwmDcConverter::applyControl(int controlValue)
{
    auto pwm {static_cast<int>(std::round(controlValue * getMaxControl() / 100.0))};

    if(pwm != m_pwm)
    {
        ledcWrite(m_ledChannel, pwm);
        m_pwm = pwm;
    }
}