#include "MqttSolarControllerBridge.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <sstream>
#include <iomanip>
#include "Utility.h"
#include "Logger.h"

static constexpr char TAG[] = "Bridge";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MqttSolarControllerBridge::MqttSolarControllerBridge(MqttClient& mqttClient, BatteryProfileSelector& profileSelector, std::string_view deviceId)
    : m_mqttClient{mqttClient},
      m_profileSelector{profileSelector}, 
      m_topics{deviceId}
{}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void MqttSolarControllerBridge::init()
{
    registerCommands();

    // On every (re)connect: republish HA discovery + current profile state.
    // Discovery is re-published so HA picks it up even if HA restarted while
    // the controller was offline.
    m_mqttClient.onConnect([this]()
    {
        publishDiscovery();
        publishProfileState();
    });
}

void MqttSolarControllerBridge::publishTelemetry(const MeasurementsIf& pvMeas, const MeasurementsIf& battMeas, BatteryManager::Mode  mode, int mpptControl_pct, unsigned long duration_ms)
{
    m_telemetryDurationTimer.update();

    if(!m_telemetryDurationTimer.active() || m_telemetryDurationTimer.getDuration() >= duration_ms)
    {
        m_telemetryDurationTimer.trigger();

        int pvPower_mW = (pvMeas.getVoltage_mV() * pvMeas.getCurrent_mA()) / 1000;
        int battPower_mW = (battMeas.getVoltage_mV() * battMeas.getCurrent_mA()) / 1000;
        int efficiency_pct = (pvPower_mW > 0) ? (battPower_mW * 100) / pvPower_mW : 0;

        // High-frequency sensor readings — no retain, fresh value supersedes stale
        m_mqttClient.publish(m_topics.pvVoltage(), floatToString(pvMeas.getVoltage_mV() / 1000.0, 3));        // Convert mV to V for more human-friendly telemetry
        m_mqttClient.publish(m_topics.pvCurrent(), floatToString(pvMeas.getCurrent_mA() / 1000.0, 3));        // Convert mA to A for more human-friendly telemetry
        m_mqttClient.publish(m_topics.pvPower(), floatToString(pvPower_mW / 1000.0, 3));                      // Convert mW to W for more human-friendly telemetry
        m_mqttClient.publish(m_topics.batteryVoltage(), floatToString(battMeas.getVoltage_mV() / 1000.0, 3)); // Convert mV to V for more human-friendly telemetry
        m_mqttClient.publish(m_topics.batteryCurrent(), floatToString(battMeas.getCurrent_mA() / 1000.0, 3)); // Convert mA to A for more human-friendly telemetry

        // State topics — retain so HA shows last known value after reconnect
        m_mqttClient.publish(m_topics.chargingMode(), batteryModeToString(mode), /*retain=*/true);
        m_mqttClient.publish(m_topics.controlSignalPct(), std::to_string(mpptControl_pct), /*retain=*/true);
        m_mqttClient.publish(m_topics.efficiencyPct(), std::to_string(efficiency_pct), /*retain=*/true);
    }
}

// ---------------------------------------------------------------------------
// Private — command subscriptions
// ---------------------------------------------------------------------------

void MqttSolarControllerBridge::registerCommands()
{
    m_mqttClient.subscribe(m_topics.batteryTypeSet(), [this](std::string_view payload)
    {
        onBatteryTypeSet(payload);
    });

    m_mqttClient.subscribe(m_topics.maxVoltageSet(), [this](std::string_view payload)
    {
        handleIntCommand("max_voltage", payload, [this](int value) { return m_profileSelector.setMaxVoltage(value); });
    });

    m_mqttClient.subscribe(m_topics.rechargeVoltageSet(), [this](std::string_view payload)
    {
        handleIntCommand("recharge_voltage", payload, [this](int value) { return m_profileSelector.setRechargeVoltage(value); });
    });

    m_mqttClient.subscribe(m_topics.prechargeVoltageSet(), [this](std::string_view payload)
    {
        handleIntCommand("precharge_voltage", payload, [this](int value) { return m_profileSelector.setPrechargeVoltage(value); });
    });

    m_mqttClient.subscribe(m_topics.loadDisconnectVoltageSet(), [this](std::string_view payload)
    {
        handleIntCommand("load_disconnect_voltage", payload, [this](int value) { return m_profileSelector.setLoadDisconnectVoltage(value); });
    });

    m_mqttClient.subscribe(m_topics.maxChargingCurrentSet(), [this](std::string_view payload)
    {
        handleIntCommand("max_charging_current", payload, [this](int value) { return m_profileSelector.setMaxChargingCurrent(value); });
    });
}

// ---------------------------------------------------------------------------
// Private — command handlers
// ---------------------------------------------------------------------------

void MqttSolarControllerBridge::onBatteryTypeSet(std::string_view payload)
{
    auto type = stringToBatteryType(payload);
    if (!type.has_value())
    {
        ESP_LOGW(TAG, "Unknown battery_type payload: %.*s",
                      static_cast<int>(payload.size()), payload.data());
        return;
    }

    auto result = m_profileSelector.setProfileType(*type);
    if (result != BatteryProfileSelector::Result::SUCCESS)
    {
        ESP_LOGE(TAG, "setProfileType failed (err=%d)", static_cast<int>(result));
        return;
    }

    ESP_LOGI(TAG, "Battery type set to %.*s", static_cast<int>(payload.size()), payload.data());
    publishProfileState();
}

void MqttSolarControllerBridge::handleIntCommand(std::string_view name, std::string_view payload, std::function<BatteryProfileSelector::Result(int)> setterFn)
{
    int value{};
    if (!parseIntSafe(payload, value))
    {
        ESP_LOGW(TAG, "Invalid %.*s payload: %.*s", static_cast<int>(name.size()), name.data(), static_cast<int>(payload.size()), payload.data());
        return;
    }

    // Attempt to set the new value; if validation fails, log and reject without restarting.
    auto result = setterFn(value);

    if (result != BatteryProfileSelector::Result::SUCCESS)
    {
        ESP_LOGW(TAG, "Setting %.*s=%d rejected (err=%d)", static_cast<int>(name.size()), name.data(), value, static_cast<int>(result));
        return;
    }

    ESP_LOGI(TAG, "%.*s set to %d", static_cast<int>(name.size()), name.data(), value);
    publishProfileState();
}



// ---------------------------------------------------------------------------
// Private — profile state publish
// ---------------------------------------------------------------------------

void MqttSolarControllerBridge::publishProfileState()
{
    const BatteryProfile& profile = m_profileSelector.getCurrentProfile();

    m_mqttClient.publish(m_topics.batteryTypeState(),
                         batteryTypeToString(m_profileSelector.getCurrentType()), /*retain=*/true);
    m_mqttClient.publish(m_topics.maxVoltageState(),
                         std::to_string(profile.maxVoltage_mV), /*retain=*/true);
    m_mqttClient.publish(m_topics.rechargeVoltageState(),
                         std::to_string(profile.rechargeVoltage_mV), /*retain=*/true);
    m_mqttClient.publish(m_topics.prechargeVoltageState(),
                         std::to_string(profile.prechargeVoltage_mV), /*retain=*/true);
    m_mqttClient.publish(m_topics.loadDisconnectVoltageState(),
                         std::to_string(profile.loadDisconnectVoltage_mV), /*retain=*/true);
    m_mqttClient.publish(m_topics.maxChargingCurrentState(),
                         std::to_string(profile.maxChargingCurrent_mA), /*retain=*/true);
}

// ---------------------------------------------------------------------------
// Private — HA MQTT Discovery
// ---------------------------------------------------------------------------

void MqttSolarControllerBridge::publishDiscovery()
{
    JsonDocument doc;
    const std::string& id = m_topics.deviceId();

    // Reusable lambda — populates the HA device block (groups all entities under one device)
    auto addDevice = [&]()
    {
        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0] = id;
        device["name"]           = "Solar Charge Controller";
        device["model"]          = "SolarChargeController v1.0";
        device["manufacturer"]   = "DIY";
    };

    // --- Sensor entities (telemetry) ---
    struct SensorEntry
    {
        const char* objectId;
        const char* name;
        std::string stateTopic;
        const char* unit;              // nullptr if not applicable
        const char* deviceClass;       // nullptr if not applicable
        int         displayPrecision;  // -1 = no suggestion (non-numeric / %-integer sensors)
        bool        retain;
    };

    const SensorEntry sensors[] = {
        { "pv_voltage",         "PV Voltage",          m_topics.pvVoltage(),        "V", "voltage", 3,  false },
        { "pv_current",         "PV Current",          m_topics.pvCurrent(),        "A", "current", 3,  false },
        { "pv_power",           "PV Power",            m_topics.pvPower(),          "W", "power",   3,  false },
        { "battery_voltage",    "Battery Voltage",     m_topics.batteryVoltage(),   "V", "voltage", 3,  false },
        { "battery_current",    "Battery Current",     m_topics.batteryCurrent(),   "A", "current", 3,  false },
        { "charging_mode",      "Charging Mode",       m_topics.chargingMode(),     nullptr, nullptr, -1, true },
        { "control_signal_pct", "Control Signal",      m_topics.controlSignalPct(), "%",  nullptr,   -1, true  },
        { "efficiency_pct",     "Charging Efficiency", m_topics.efficiencyPct(),    "%",  nullptr,   -1, true },
    };

    for (const auto& s : sensors)
    {
        doc.clear();
        doc["name"]               = s.name;
        doc["unique_id"]          = id + "_" + s.objectId;
        doc["state_topic"]        = s.stateTopic;
        if (s.unit)               doc["unit_of_measurement"]      = s.unit;
        if (s.deviceClass)        doc["device_class"]              = s.deviceClass;
        if (s.displayPrecision >= 0)
        {
            doc["state_class"]                = "measurement";
            doc["suggested_display_precision"] = s.displayPrecision;
        }
        doc["availability_topic"] = m_topics.availability();
        addDevice();

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(m_topics.hassDiscovery("sensor", s.objectId), payload, /*retain=*/true);
    }

    // --- Select entity: battery type ---
    doc.clear();
    doc["name"]               = "Battery Type";
    doc["unique_id"]          = id + "_battery_type";
    doc["command_topic"]      = m_topics.batteryTypeSet();
    doc["state_topic"]        = m_topics.batteryTypeState();
    doc["options"][0]         = "LIION_3S";
    doc["options"][1]         = "LIION_4S";
    doc["options"][2]         = "LIFEPO4_4S";
    doc["options"][3]         = "CUSTOM";
    doc["availability_topic"] = m_topics.availability();
    addDevice();
    {
        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(m_topics.hassDiscovery("select", "battery_type"), payload, /*retain=*/true);
    }

    // --- Number entities: profile voltage / current overrides ---
    struct NumberEntry
    {
        const char* objectId;
        const char* name;
        std::string commandTopic;
        std::string stateTopic;
        const char* unit;
        int         min;
        int         max;
        int         step;
    };

    const NumberEntry numbers[] = {
        { "max_voltage",             "Max Voltage",             m_topics.maxVoltageSet(),            m_topics.maxVoltageState(),            "mV",  1000, 30000, 100 },
        { "recharge_voltage",        "Recharge Voltage",        m_topics.rechargeVoltageSet(),       m_topics.rechargeVoltageState(),       "mV",  1000, 30000, 100 },
        { "precharge_voltage",       "Precharge Voltage",       m_topics.prechargeVoltageSet(),      m_topics.prechargeVoltageState(),      "mV",  1000, 30000, 100 },
        { "load_disconnect_voltage", "Load Disconnect Voltage", m_topics.loadDisconnectVoltageSet(), m_topics.loadDisconnectVoltageState(), "mV",  1000, 30000, 100 },
        { "max_charging_current",    "Max Charging Current",    m_topics.maxChargingCurrentSet(),    m_topics.maxChargingCurrentState(),    "mA",  100,  10000, 100 },
    };

    for (const auto& n : numbers)
    {
        doc.clear();
        doc["name"]               = n.name;
        doc["unique_id"]          = id + "_" + n.objectId;
        doc["command_topic"]      = n.commandTopic;
        doc["state_topic"]        = n.stateTopic;
        doc["unit_of_measurement"]= n.unit;
        doc["min"]                = n.min;
        doc["max"]                = n.max;
        doc["step"]               = n.step;
        doc["mode"]               = "box";
        doc["availability_topic"] = m_topics.availability();
        addDevice();

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(m_topics.hassDiscovery("number", n.objectId), payload, /*retain=*/true);
    }
}

// ---------------------------------------------------------------------------
// Private — static converters
// ---------------------------------------------------------------------------

std::string_view MqttSolarControllerBridge::batteryModeToString(BatteryManager::Mode mode)
{
    switch (mode)
    {
        case BatteryManager::Mode::Idle:      return "Idle";
        case BatteryManager::Mode::Precharge: return "Precharge";
        case BatteryManager::Mode::CC:        return "CC";
        case BatteryManager::Mode::CV:        return "CV";
        case BatteryManager::Mode::Done:      return "Done";
        case BatteryManager::Mode::Fault:     return "Fault";
        default:                              return "Unknown";
    }
}

std::string_view MqttSolarControllerBridge::batteryTypeToString(BatteryConfig::BatteryType type)
{
    switch (type)
    {
        case BatteryConfig::BatteryType::LIION_3S:   return "LIION_3S";
        case BatteryConfig::BatteryType::LIION_4S:   return "LIION_4S";
        case BatteryConfig::BatteryType::LIFEPO4_4S: return "LIFEPO4_4S";
        case BatteryConfig::BatteryType::CUSTOM:     return "CUSTOM";
        default:                                     return "Unknown";
    }
}

std::optional<BatteryConfig::BatteryType> MqttSolarControllerBridge::stringToBatteryType(std::string_view str)
{
    if (str == "LIION_3S")   return BatteryConfig::BatteryType::LIION_3S;
    if (str == "LIION_4S")   return BatteryConfig::BatteryType::LIION_4S;
    if (str == "LIFEPO4_4S") return BatteryConfig::BatteryType::LIFEPO4_4S;
    if (str == "CUSTOM")     return BatteryConfig::BatteryType::CUSTOM;
    return std::nullopt;
}
