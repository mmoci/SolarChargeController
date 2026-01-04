#pragma once

#include <Arduino.h>

class Device
{
    protected:
    uint8_t m_pin{};

    public:
    Device(uint8_t pin) : m_pin{pin}
    {}

    virtual void init();
    virtual void update();

    virtual ~Device() = default;
};