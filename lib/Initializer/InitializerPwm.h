#pragma once

#include "Initializer.h"
#include "PwmDcConverter.h"
#include "SensorINA226.h"
#include "Config.h"

class InitializerPwm : public Initializer
{
    public:
    void init() override;
    void update() override;
    MeasurementsIf& getPvMeasurements() override;
    MeasurementsIf& getBatteryMeasurements() override;
    ActuatorIf& getActuator() override;

    private:
    static constexpr uint8_t I2C_SDA_PIN {21};
    static constexpr uint8_t I2C_SCL_PIN {22};
    static constexpr uint8_t PWM_PIN     {32}; // GPIO pin for PWM output to DC converter
    
    PwmDcConverter pwmActuator{PWM_PIN};
    SensorINA226 pvSensor{SensorConfig::PV_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::PV_SHUNT_mOhm};
    SensorINA226 batterySensor{SensorConfig::BATTERY_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::BATTERY_SHUNT_mOhm};
};