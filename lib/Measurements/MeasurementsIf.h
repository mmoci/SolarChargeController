#pragma once

class MeasurementsIf
{
    public:
    virtual int getVoltage_mV() const = 0;
    virtual int getCurrent_mA() const = 0;
    virtual bool isMeasurementValid() const {return true;}
    virtual unsigned long lastTimeUpdated() const {return 0;}
    virtual bool hasMeasurementDelay() const { return false; } // Returns true when the hardware read cycle is slow relative to the Arduino

    virtual ~MeasurementsIf() = default;
};