#pragma once

#include "Device.h"
#include "SensorI2C.h"
#include "MeasurementsIf.h"

class SensorINAIf : public Device, public SensorI2C, public MeasurementsIf
{
    public:
    // If no successful read arrives within this window the measurement is
    // considered stale. INA226 reads every ~6ms so 100ms gives ~16x headroom
    // for transient I2C hiccups before the controller reacts.
    static constexpr unsigned long STALE_TIMEOUT_MS{100};

    SensorINAIf(uint16_t deviceAddress) : SensorI2C(deviceAddress)
    {}

    virtual ~SensorINAIf() = default;

    // Device overrides
    void init() override;
    void update() override;

    // MeasurementsIf overrides
    int getCurrent_mA() const override {return m_current_mA;}
    int getVoltage_mV() const override {return m_voltage_mV;}
    bool isMeasurementValid() const override;
    unsigned long measurementAge() const override;
    bool isMeasurementUpdated() override;
    bool isMeasurementSettled() const override;

    protected:
    int m_current_mA{};
    int m_voltage_mV{};
    unsigned long m_lastUpdateTime{};
    unsigned long m_lastMeasurementAge{};

    virtual void setShuntCalibrationRegister() = 0;
    virtual void setConfigurationRegister(uint16_t configValue) = 0;
};