#include "ChargeController.h"
#include "NullSensor.h"        // For the dummy PV sensor
#include "SensorINA226.h"      // Concrete sensor
#include "PwmDcConverter.h"    // Concrete PWM actuator
#include "DPSxDcConverter.h"   // Concrete DPS actuator

static constexpr uint8_t I2C_SCL_PIN             {22};
static constexpr uint8_t I2C_SDA_PIN             {21};
static constexpr uint16_t PV_SENSOR_ADDRESS      {0x40};
static constexpr uint16_t BATTERY_SENSOR_ADDRESS {0x41};
static constexpr uint8_t  PWM_PIN                {32};

#ifdef DPS_DC_CONVERTER
    DPSxDcConverter dpsDcConverter{};
   
    // If you have a PV sensor, use SensorINA226 here instead of Null
    NullSensor pvSensor{}; 
    //SensorINA226 pvSensor{PV_SENSOR_ADDRESS};
    
    ChargeController controller{&pvSensor, &dpsDcConverter, &dpsDcConverter};
#else
    SensorINA226 pvSensor{PV_SENSOR_ADDRESS};
    SensorINA226 batterySensor{BATTERY_SENSOR_ADDRESS};
    PwmDcConverter pwmActuator{PWM_PIN};
    ChargeController controller{&pvSensor, &batterySensor, &pwmActuator};
#endif

void setup() 
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    #ifdef DPS_DC_CONVERTER
    dpsDcConverter.init();
    #else
    pvSensor.init();
    batterySensor.init();
    pwmActuator.init();
    #endif

    controller.init();
}

void loop() 
{
    #ifdef DPS_DC_CONVERTER
    dpsDcConverter.update();
    #else
    pvSensor.update();
    batterySensor.update();
    pwmActuator.update();
    #endif

    controller.update();
    delay(3);
}