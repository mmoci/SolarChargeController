#ifdef MQTT_CLIENT
#include "OtaHandler.h"
#include <Arduino.h>

void OtaHandler::init()
{
    ArduinoOTA.setHostname(std::string{m_config.hostname}.c_str());
    ArduinoOTA.setPassword(std::string{m_config.password}.c_str());

    ActuatorIf* actuator = m_actuator;
    ArduinoOTA.onStart([actuator]()
    {
        Serial.println("[OTA] Starting update — stopping actuator");
        if (actuator)
            actuator->applyControl(0);
    });
    ArduinoOTA.onEnd([]()
    {
        Serial.println("[OTA] Update complete");
    });
    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("[OTA] Error[%u]\n", error);
    });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Ready — hostname: %s\n", std::string{m_config.hostname}.c_str());
}

void OtaHandler::handle()
{
    ArduinoOTA.handle();
}

#endif // MQTT_CLIENT