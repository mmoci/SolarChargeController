#pragma once

class MeasurementsIf
{
    public:
    virtual int getVoltage_mV() const = 0;
    virtual int getCurrent_mA() const = 0;
    virtual bool isMeasurementValid() const {return true;}
    virtual bool isMeasurementUpdated() {return false;}

    virtual ~MeasurementsIf() = default;

    protected:
    virtual unsigned long measurementAge() const {return 0;}
};