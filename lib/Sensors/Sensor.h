#pragma once

#include <Arduino.h>

class Sensor
{
    public:
    virtual void init() = 0;
    virtual void update() = 0;
    virtual ~Sensor() = default;
};