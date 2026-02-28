#pragma once

#include "Utility.h"

// TODO: Config namespace with static variables

class MpptController
{
    public:
    struct MpptOutputData
    {
        int control{};
    };

    static constexpr int DEFAULT_STEP{1};
    static constexpr int MIN_CONTROL_VALUE{0};
    static constexpr int MAX_CONTROL_VALUE{100};

    void init();
    void update(Measurements mpptInputData);
    int getRequestedOutput() const {return m_outputData.control;}
    int getOutputStep() const {return m_step;}
    void setOutputStep(int step) {m_step = step;}

    private:
    enum class Direction
    {
        Down,
        Up
    };

    Measurements m_pvMeasurements{};
    MpptOutputData m_outputData{};
    Direction m_direction{};
    int m_step{};
};