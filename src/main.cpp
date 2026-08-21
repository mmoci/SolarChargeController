#include "ChargeController.h"
#include "BatteryProfileSelector.h"
#include "Initializer.h"
#include "MqttSolarControllerBridge.h"
#include "OtaHandler.h"
#include "Utility.h"
#include "Logger.h"
#include "SdLogger.h"
#include "Secrets.h"

static SdLogger sdLogger{5}; // CS pin 5; no-op when SD_CARD not defined

static constexpr char TAG[] = "Main";

#ifdef MQTT_CLIENT
static constexpr std::string_view DEVICE_ID {"solar_controller_1"};
static const std::string AVAILABILITY_TOPIC {"solar/" + std::string{DEVICE_ID} + "/availability"};

MqttClient::Config mqttConfig{
    .broker        = MQTT_BROKER,
    .port          = 1883,
    .clientId      = DEVICE_ID,
    .username      = MQTT_USERNAME,
    .password      = MQTT_PASSWORD,
    .wifiSsid      = WIFI_SSID,
    .wifiPassword  = WIFI_PASSWORD,
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

ChargeController controller{
    &Initializer::getInstance().getPvMeasurements(),
    &Initializer::getInstance().getBatteryMeasurements(),
    &Initializer::getInstance().getActuator(),
    &Initializer::getInstance().getMpptStrategy(),
    &profileSelector};

#ifdef MQTT_CLIENT
OtaHandler otaHandler{{.hostname = DEVICE_ID, .password = OTA_PASSWORD}, &Initializer::getInstance().getActuator()};
#endif

void setup() 
{
    Serial.begin(115200);

    #if defined(ESP32) || defined(ESP_PLATFORM)
    esp_log_level_set("*", ESP_LOG_DEBUG);
    #endif

    ESP_LOGI("BOOT", "Reset reason: %d", (int)esp_reset_reason());
    ESP_LOGI(TAG, "setup() start");

    sdLogger.init();

    #ifdef MQTT_CLIENT
    ESP_LOGI(TAG, "Initialising MQTT client...");
    mqttClient.init();
    ESP_LOGI(TAG, "mqttClient.init() done");
    bridge.init();
    ESP_LOGI(TAG, "bridge.init() done");
    #endif

    Initializer::getInstance().init();
    ESP_LOGI(TAG, "Hardware config init() done");

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
    Initializer::getInstance().update();

    controller.update();

    #ifdef MQTT_CLIENT
    bridge.publishTelemetry(
            Initializer::getInstance().getPvMeasurements(),
            Initializer::getInstance().getBatteryMeasurements(),
            controller.getBatteryMode(),
            controller.getMpptControl());
    #endif

    if(millis() - sdLogger.getLastFlushTime() >= SdLogger::FLUSH_INTERVAL_MS)
        sdLogger.flush();
    
    if(controller.isPvDisconnected())
    {
        #ifdef MQTT_CLIENT
        mqttClient.publish(AVAILABILITY_TOPIC, "offline", true);
        #endif
        ESP_LOGI(TAG, "System ready for deep sleep — entering sleep mode for %llu seconds", ChargeControllerConfig::DEFAULT_DEEP_SLEEP_DURATION_S);
        controller.requestDeepSleep();
    }

    delay(3);
}