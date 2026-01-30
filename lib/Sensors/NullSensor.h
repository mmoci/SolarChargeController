#pragma once
#include "Device.h"
#include "MeasurementsIf.h"

class NullSensor : public Device, public MeasurementsIf 
{
    public:
    void update() override {}
    void init() override {}

    int getVoltage_mV() const override { return 0; }
    int getCurrent_mA() const override { return 0; }
};