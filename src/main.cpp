#include "ChargeController.h"
#include "BatteryProfileSelector.h"
#include "SensorINA226.h"      // Concrete sensor
#include "PwmDcConverter.h"    // Concrete PWM actuator
#include "DPSxDcConverter.h"   // Concrete DPS actuator
#include "DPSxMeasurements.h"
#include "MqttClient.h"
#include "MqttSolarControllerBridge.h"
#include "OtaHandler.h"
#include "Utility.h"
#include "Logger.h"

static constexpr char TAG[] = "Main";

static constexpr uint8_t I2C_SCL_PIN             {22};
static constexpr uint8_t I2C_SDA_PIN             {21};
static constexpr uint8_t PWM_PIN                 {32};
// Serial2 used for DPS Modbus RTU (9600 8N1).
// ESP32 default Serial2 pins; re-assign here if rerouted on the PCB.
static constexpr uint8_t SERIAL2_RX_PIN          {16};
static constexpr uint8_t SERIAL2_TX_PIN          {17};


#ifdef MQTT_CLIENT
static constexpr std::string_view DEVICE_ID {"solar_controller_1"};
// Constructed as std::string (owning) because string_view cannot hold a concatenated result.
// Matches MqttSolarControllerTopicBuilder::availability() = "solar/{DEVICE_ID}/availability".
static const std::string AVAILABILITY_TOPIC {"solar/" + std::string{DEVICE_ID} + "/availability"};

MqttClient::Config mqttConfig{
    .broker        = "192.168.1.2",
    .port          = 1883,
    .clientId      = DEVICE_ID,
    .username      = "moci_mqtt",
    .password      = "Mm21101981",
    .wifiSsid      = "Net_2110",
    .wifiPassword  = "MM2110981340709!",
    .willTopic     = AVAILABILITY_TOPIC,
    .willPayload   = "offline",
    .onlinePayload = "online",
    .staticIp      = IPAddress(192, 168, 1, 50),
    .gateway       = IPAddress(192, 168, 1, 1),
    .subnet        = IPAddress(255, 255, 255, 0),
    .dns1          = IPAddress(192, 168, 1, 1),
};
MqttClient mqttClient{mqttConfig};
#endif

BatteryProfileSelector profileSelector{};

#ifdef MQTT_CLIENT
MqttSolarControllerBridge bridge{mqttClient, profileSelector, DEVICE_ID};
#endif

#ifdef DPS_DC_CONVERTER
    DPSxDcConverter dpsDcConverter{};
    DPSxMeasurements pvMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Input};
    DPSxMeasurements batteryMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Output};
    ChargeController controller{&pvMeasurements, &batteryMeasurements, &dpsDcConverter, &profileSelector};
    #ifdef MQTT_CLIENT
    OtaHandler otaHandler{{.hostname = DEVICE_ID, .password = "ota_password"}, &dpsDcConverter};
    #endif
#else
    SensorINA226 pvSensor{SensorConfig::PV_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::PV_SHUNT_mOhm};
    SensorINA226 batterySensor{SensorConfig::BATTERY_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::BATTERY_SHUNT_mOhm};
    PwmDcConverter pwmActuator{PWM_PIN};
    ChargeController controller{&pvSensor, &batterySensor, &pwmActuator, &profileSelector};
    #ifdef MQTT_CLIENT
    OtaHandler otaHandler{{.hostname = DEVICE_ID, .password = "ota_password"}, &pwmActuator};
    #endif
#endif

void setup() 
{
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    ESP_LOGI(TAG, "setup() start");

    #ifdef MQTT_CLIENT
    ESP_LOGI(TAG, "Initialising MQTT client...");
    mqttClient.init();
    ESP_LOGI(TAG, "mqttClient.init() done");
    #endif

    #ifdef MQTT_CLIENT
    bridge.init();
    ESP_LOGI(TAG, "bridge.init() done");
    #endif

    #ifdef DPS_DC_CONVERTER
    Serial2.begin(9600, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
    dpsDcConverter.init();
    ESP_LOGI(TAG, "dpsDcConverter.init() done");
    #else
    pvSensor.init();
    batterySensor.init();
    pwmActuator.init();
    #endif

    controller.init();
    ESP_LOGI(TAG, "controller.init() done");

    #ifdef MQTT_CLIENT
    otaHandler.init();
    ESP_LOGI(TAG, "otaHandler.init() done");
    #endif
    ESP_LOGI(TAG, "setup() complete");
}

void loop() 
{
    #ifdef MQTT_CLIENT
    mqttClient.process();
    otaHandler.handle();
    #endif

    // Update sensors and controller first so telemetry always reads fresh values
    #ifdef DPS_DC_CONVERTER
    dpsDcConverter.update();
    #else
    pvSensor.update();
    batterySensor.update();
    pwmActuator.update();
    #endif

    controller.update();

    #ifdef MQTT_CLIENT
    static Timer telemetryTimer{};
    telemetryTimer.update();
    if (!telemetryTimer.active() || telemetryTimer.getDuration() >= 5000)
    {
        telemetryTimer.trigger();
        #ifdef DPS_DC_CONVERTER
        bridge.publishTelemetry(pvMeasurements, batteryMeasurements, controller.getBatteryMode(), controller.getMpptControl());
        #else
        bridge.publishTelemetry(pvSensor, batterySensor, controller.getBatteryMode(), controller.getMpptControl());
        #endif
    }
    #endif

    delay(3);
}