#pragma once

#include <Arduino.h>

class Device
{
    public:
    virtual void init();
    virtual void update();

    virtual ~Device() = default;
};