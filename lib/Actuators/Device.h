#pragma once

#include <Arduino.h>

class Device
{
    public:
    virtual void init() = 0;
    virtual void update() = 0;

    virtual ~Device() = default;
};