#pragma once
#include <optional>

class MeasurementsIf
{
    public:
    virtual int getVoltage_mV() const = 0;
    virtual int getCurrent_mA() const = 0;
    virtual std::optional<int> getOpenCircuitVoltage_mV() const {return std::nullopt;} // Only relevant for PV measurements with input voltage regulation strategy, default is std::nullopt for non-PV or non-regulating actuators
    virtual bool isMeasurementValid() const {return true;}
    virtual bool isMeasurementUpdated() {return false;}
    virtual bool isMeasurementSettled() const {return true;}

    virtual ~MeasurementsIf() = default;

    protected:
    virtual unsigned long measurementAge() const {return 0;}
};