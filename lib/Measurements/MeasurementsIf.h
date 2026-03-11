#pragma once

class MeasurementsIf
{
    public:
    virtual int getVoltage_mV() const = 0;
    virtual int getCurrent_mA() const = 0;
    virtual bool isMeasurementValid() const {return true;}
    virtual unsigned long lastTimeUpdated() const {return 0;}

    virtual ~MeasurementsIf() = default;
};