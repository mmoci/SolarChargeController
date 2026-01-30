#pragma once

class MeasurementsIf
{
    public:
    virtual int getVoltage_mV() const = 0;
    virtual int getCurrent_mA() const = 0;

    virtual ~MeasurementsIf() = default;
};