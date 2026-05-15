#pragma once

#include "Device.h"
#include "ActuatorIf.h"
#include <cmath>

namespace PwmDcConverterConfig
{
    static constexpr int DEFAULT_RESOLUTION  {8}; // bits
    static constexpr int DEFAULT_LED_CHANNEL {0};
    static constexpr int DEFAULT_FREQUENCY   {10000};

    static constexpr ActuatorIf::ControlMode CONTROL_MODE {ActuatorIf::ControlMode::DUTY_CYCLE};
    inline static int MAX_MPPT_CONTROL_VALUE              {static_cast<int>(std::roundl(std::pow(2, DEFAULT_RESOLUTION)))};
    inline static int DEFAULT_MPPT_CONTROL_VALUE          {MAX_MPPT_CONTROL_VALUE / 2};
}

class PwmDcConverter : public Device, public ActuatorIf
{
    public:
    PwmDcConverter(uint8_t pin, int ledChannel = PwmDcConverterConfig::DEFAULT_LED_CHANNEL, int frequency = PwmDcConverterConfig::DEFAULT_FREQUENCY, int resolution = PwmDcConverterConfig::DEFAULT_RESOLUTION);

    // Device overrides
    void init() override;
    void update() override;

    // ActuatorIf overrides
    void enableOutput(bool enable, bool priority = false) override;
    bool isOutputEnabled() const override;
    ControlMode getControlMode() const override;
    int getMinControl() const override;
    int getMaxControl() const override;
    bool hasMeasurements() const override;
    void applyControl(int controlValue) override;

    private:
    ControlMode m_controlMode{PwmDcConverterConfig::CONTROL_MODE};
    uint8_t m_pin{};
    int m_ledChannel{};
    int m_frequency{};
    int m_resolution{};
    int m_pwm{};
};