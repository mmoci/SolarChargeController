#pragma once
#ifdef MQTT_CLIENT

#include <ArduinoOTA.h>
#include <string_view>
#include "ActuatorIf.h"

/**
 * @class OtaHandler
 * @brief Thin wrapper around ArduinoOTA for over-the-air firmware updates.
 *
 * Requires WiFi to already be connected (i.e. MqttClient::init() must have
 * run first). Only compiled when MQTT_CLIENT is defined, since OTA is
 * unreachable without WiFi.
 *
 * On update start the actuator is driven to 0 before the flash begins.
 * loop() is blocked during OTA so the charge controller cannot act; holding
 * the last duty cycle for several seconds risks overcharging in CV mode.
 */
class OtaHandler
{
public:
    struct Config
    {
        /// mDNS hostname — device appears as {hostname}.local on the network.
        std::string_view hostname;
        /// Password required by the OTA client (Arduino IDE / PlatformIO upload).
        std::string_view password;
    };

    /**
     * @brief Construct with hardware dependencies.
     * @param config    Hostname and password.
     * @param actuator  Charge-path actuator; driven to 0 when update starts.
     */
    OtaHandler(const Config& config, ActuatorIf* actuator) : m_config{config}, m_actuator{actuator}
    {}

    /**
     * @brief Register ArduinoOTA callbacks and start the OTA service.
     *        Call once from setup(), after WiFi is connected.
     */
    void init();

    /**
     * @brief Drive the OTA state machine. Call every loop() iteration.
     */
    void handle();

private:
    Config       m_config;
    ActuatorIf*  m_actuator{};
};

#endif // MQTT_CLIENT
