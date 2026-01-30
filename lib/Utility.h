#pragma once

#include <Arduino.h>

class Timer
{
    private:
    unsigned long start{};
    unsigned long duration{};

    public:
    void reset()
    {
        start = 0;
        duration = 0;
    }

    void trigger()
    {
        reset();
        start = millis();
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

    bool active()
    {
        return start;
    }

    unsigned long getDuration() const {return duration;}
};

struct Measurements
{
    int voltage_mV{};
    int current_mA{};
};