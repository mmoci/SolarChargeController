#include <Arduino.h>
#include "ChargeController.h"

static constexpr uint8_t I2C_SCL_PIN             {22};
static constexpr uint8_t I2C_SDA_PIN             {21};
static constexpr uint16_t PV_SENSOR_ADDRESS      {0x123};
static constexpr uint16_t BATTERY_SENSOR_ADDRESS {0x123};
static constexpr uint8_t  PWM_PIN                {32};

ChargeController solarChargeController{PV_SENSOR_ADDRESS, BATTERY_SENSOR_ADDRESS, PWM_PIN};

void setup() 
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    solarChargeController.init();
}

void loop() 
{
    solarChargeController.update();
}