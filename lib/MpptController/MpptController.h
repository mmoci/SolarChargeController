#pragma once

#include "CurrentSensor.h"
#include "VoltageSensor.h"
#include "Utility.h"

class MpptController
{
    private:
    enum class Direction
    {
        Down,
        Up
    };

    static constexpr int DEFAULT_PWM_STEP{1};

    Measurements m_pvMeasurements{};
    int m_pwmDuty{};
    Direction m_direction{};
    int m_step{};

    public:
    void init();
    void update(const Measurements& pvMeasurements);
    int getRequestedPwmDuty() const {return m_pwmDuty;}
    int getPwmStep() const {return m_step;}
    void setPwmStep(int step) {m_step = step;}
};