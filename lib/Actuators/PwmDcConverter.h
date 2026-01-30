#pragma once

#include "Device.h"
#include "ActuatorIf.h"
#include <cmath>
#include "Config.h"

class PwmDcConverter : public Device, public ActuatorIf
{
    public:
    PwmDcConverter(uint8_t pin, int ledChannel = DcConverterConfig::DEFAULT_LED_CHANNEL, int frequency = DcConverterConfig::DEFAULT_FREQUENCY, int resolution = DcConverterConfig::DEFAULT_RESOLUTION) : 
        m_pin{pin},
        m_ledChannel{ledChannel},
        m_frequency{frequency},
        m_resolution{resolution},
        m_pwm{0}
    {}

    void init() override;
    void update() override;
    void applyControl(int pwm)
    {
        if(pwm != m_pwm)
        {
            ledcWrite(m_ledChannel, pwm);
            m_pwm = pwm;
        }
    }

    private:
    uint8_t m_pin{};
    int m_ledChannel{};
    int m_frequency{};
    int m_resolution{};
    int m_pwm{};
};

inline void PwmDcConverter::init()
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

inline void PwmDcConverter::update()
{}