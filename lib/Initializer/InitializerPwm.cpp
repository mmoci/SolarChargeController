#include "InitializerPwm.h"

void InitializerPwm::init()
{
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    pvSensor.init();
    batterySensor.init();
    pwmActuator.init();
    pwmMpptStrategy.init();
}

void InitializerPwm::update()
{
    pvSensor.update();
    batterySensor.update();
    pwmActuator.update();
}

MeasurementsIf& InitializerPwm::getPvMeasurements()
{
    return pvSensor;
}

MeasurementsIf& InitializerPwm::getBatteryMeasurements()
{
    return batterySensor;
}

ActuatorIf& InitializerPwm::getActuator()
{
    return pwmActuator;
}

MpptStrategyIf& InitializerPwm::getMpptStrategy()
{
    return pwmMpptStrategy;
}