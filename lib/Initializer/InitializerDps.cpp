#include "InitializerDps.h"

void InitializerDps::init()
{
    Serial2.begin(9600, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
    dpsDcConverter.init();
}

void InitializerDps::update()
{
    // Update the DPS DC converter to refresh measurements before the controller reads them
    dpsDcConverter.update();
}

MeasurementsIf& InitializerDps::getPvMeasurements()
{
    // Return reference to the DPS-specific PV measurements
    return pvMeasurements;
}

MeasurementsIf& InitializerDps::getBatteryMeasurements()
{
    // Return reference to the DPS-specific battery measurements
    return batteryMeasurements;
}

ActuatorIf& InitializerDps::getActuator()
{
    // Return reference to the DPS-specific actuator (DC converter)
    return dpsDcConverter;
}
