#ifdef MQTT_CLIENT
#include "OtaHandler.h"
#include <Arduino.h>
#include "Logger.h"

static constexpr char TAG[] = "OtaHandler";

void OtaHandler::init()
{
    ArduinoOTA.setHostname(std::string{m_config.hostname}.c_str());
    ArduinoOTA.setPassword(std::string{m_config.password}.c_str());

    ActuatorIf* actuator = m_actuator;
    ArduinoOTA.onStart([actuator]()
    {
        ESP_LOGI(TAG, "Starting update — stopping actuator");
        if (actuator)
            actuator->applyControl(0);
    });
    ArduinoOTA.onEnd([]()
    {
        ESP_LOGI(TAG, "Update complete");
    });
    ArduinoOTA.onError([](ota_error_t error)
    {
        ESP_LOGE(TAG, "Error[%u]", error);
    });

    ArduinoOTA.begin();
    ESP_LOGI(TAG, "Ready — hostname: %s", std::string{m_config.hostname}.c_str());
}

void OtaHandler::handle()
{
    ArduinoOTA.handle();
}

#endif // MQTT_CLIENT