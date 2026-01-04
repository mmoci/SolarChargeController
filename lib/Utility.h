#pragma once

#include <Arduino.h>

class Timer
{
    private:
    unsigned long start{};
    unsigned long duration{};

    public:
    void trigger()
    {
        start = millis();
        duration = 0;
    }

    void reset()
    {
        start = 0;
        duration = 0;
    }

    void update()
    {
        if(start == 0)
        {
            duration = 0;
            return;
        }
        duration = millis() - start;
    }

    unsigned long getDuration() const {return duration;}
};

struct Measurements
{
    int voltage_mV{};
    int current_mA{};
};