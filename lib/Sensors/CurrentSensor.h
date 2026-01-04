#pragma once

#include "Sensor.h"

class CurrentSensor : public Sensor
{
    public:
    /**
     * @brief Get the Current mA object.
     *        Positive: current flowing into the battery.
     *        Negative: current flowing out of the battery
     * 
     * @return int Returns current in milliamps.
     */
    virtual int getCurrent_mA() const = 0;
};