#include "ChargeController.h"
#include "BatteryProfileSelector.h"
#include "SensorINA226.h"      // Concrete sensor
#include "PwmDcConverter.h"    // Concrete PWM actuator
#include "DPSxDcConverter.h"   // Concrete DPS actuator
#include "DPSxMeasurements.h"
#include "MqttClient.h"
#include "MqttSolarControllerBridge.h"
#include "Utility.h"

static constexpr uint8_t I2C_SCL_PIN             {22};
static constexpr uint8_t I2C_SDA_PIN             {21};
static constexpr uint8_t  PWM_PIN                {32};
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
    .broker        = "your_broker_address",
    .port          = 1883,
    .clientId      = "your_client_id",
    .username      = "your_username",
    .password      = "your_password",
    .wifiSsid      = "your_wifi_ssid",
    .wifiPassword  = "your_wifi_password",
    .willTopic     = AVAILABILITY_TOPIC,
    .willPayload   = "offline",
    .onlinePayload = "online"
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
    ChargeController controller{&pvMeasurements, &batteryMeasurements, &dpsDcConverter};
#else
    SensorINA226 pvSensor{SensorConfig::PV_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::PV_SHUNT_mOhm};
    SensorINA226 batterySensor{SensorConfig::BATTERY_SENSOR_DEVICE_ADDRESS, SensorConfig::SensorINA226::BATTERY_SHUNT_mOhm};
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

    #ifdef MQTT_CLIENT
    bridge.init();
    #endif

    #ifdef DPS_DC_CONVERTER
    Serial2.begin(9600, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
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