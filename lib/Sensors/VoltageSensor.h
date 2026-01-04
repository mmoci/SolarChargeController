#pragma once

#include "Sensor.h"

class VoltageSensor : public Sensor
{
    public:
    virtual int getVoltage_mV() const = 0;
};