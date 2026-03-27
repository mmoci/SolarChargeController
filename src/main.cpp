#include "ChargeController.h"
#include "BatteryProfileSelector.h"
#include "SensorINA226.h"      // Concrete sensor
#include "PwmDcConverter.h"    // Concrete PWM actuator
#include "DPSxDcConverter.h"   // Concrete DPS actuator
#include "DPSxMeasurements.h"
#include "MqttClient.h"

static constexpr uint8_t I2C_SCL_PIN             {22};
static constexpr uint8_t I2C_SDA_PIN             {21};
static constexpr uint8_t  PWM_PIN                {32};


#ifdef MQTT_CLIENT
MqttClient::Config mqttConfig{
    .broker = "your_broker_address",
    .port = 1883,
    .clientId = "your_client_id",
    .username = "your_username",
    .password = "your_password",
    .wifiSsid = "your_wifi_ssid",
    .wifiPassword = "your_wifi_password",
    .willTopic = "your_will_topic",
    .willPayload = "offline",
    .onlinePayload = "online"
};
MqttClient mqttClient{mqttConfig};
#endif

BatteryProfileSelector profileSelector{};

#ifdef DPS_DC_CONVERTER
    DPSxDcConverter dpsDcConverter{};
    DPSxMeasurements pvMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Input};
    DPSxMeasurements batteryMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Output};
    ChargeController controller{&pvMeasurements, &batteryMeasurements, &dpsDcConverter};
#else
    SensorINA226 pvSensor{SensorConfig::PV_SENSOR_DEVICE_ADDRESS};
    SensorINA226 batterySensor{SensorConfig::BATTERY_SENSOR_DEVICE_ADDRESS};
    PwmDcConverter pwmActuator{PWM_PIN};
    ChargeController controller{&pvSensor, &batterySensor, &pwmActuator};
#endif

void setup() 
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    #ifdef MQTT_CLIENT
    Serial.println("[Main] Initialising MQTT client...");
    mqttClient.init();
    #endif

    profileSelector.init();

    #ifdef DPS_DC_CONVERTER
    dpsDcConverter.init();
    #else
    pvSensor.init();
    batterySensor.init();
    pwmActuator.init();
    #endif

    controller.init(profileSelector.getCurrentProfile());
}

void loop() 
{
    #ifdef MQTT_CLIENT
    mqttClient.process();
    #endif

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