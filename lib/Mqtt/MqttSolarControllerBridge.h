#pragma once

#include "MqttClient.h"
#include "MeasurementsIf.h"
#include "BatteryManager.h"
#include "BatteryProfileSelector.h"
#include <string>
#include <string_view>
#include <optional>
#include <functional>

/**
 * @class MqttSolarControllerTopicBuilder
 * @brief Generates all MQTT topic strings for the solar charge controller.
 *
 * Instance-based — every topic embeds the device ID, enabling multiple
 * devices on the same broker without topic collisions.
 *
 * Topic layout:
 *   solar/{deviceId}/availability
 *   solar/{deviceId}/telemetry/{measurement}
 *   solar/{deviceId}/profile/{field}/state    ← published by controller (retained)
 *   solar/{deviceId}/profile/{field}/set      ← subscribed by controller (published by HA)
 *   homeassistant/{component}/{deviceId}/{objectId}/config  ← HA discovery
 */
class MqttSolarControllerTopicBuilder
{
public:
    explicit MqttSolarControllerTopicBuilder(std::string_view deviceId) : m_deviceId{deviceId}, m_base{"solar/" + std::string{deviceId}}
    {}

    // Availability
    std::string availability()              const { return m_base + "/availability"; }

    // Telemetry state topics (published by controller)
    std::string pvVoltage()                 const { return m_base + "/telemetry/pv_voltage"; }
    std::string pvCurrent()                 const { return m_base + "/telemetry/pv_current"; }
    std::string pvPower()                   const { return m_base + "/telemetry/pv_power"; }
    std::string batteryVoltage()            const { return m_base + "/telemetry/battery_voltage"; }
    std::string batteryCurrent()            const { return m_base + "/telemetry/battery_current"; }
    std::string chargingMode()              const { return m_base + "/telemetry/charging_mode"; }
    std::string controlSignalPct()          const { return m_base + "/telemetry/control_signal_pct"; }

    // Profile state topics (published by controller, retain=true)
    std::string batteryTypeState()           const { return m_base + "/profile/battery_type/state"; }
    std::string maxVoltageState()            const { return m_base + "/profile/max_voltage/state"; }
    std::string rechargeVoltageState()       const { return m_base + "/profile/recharge_voltage/state"; }
    std::string prechargeVoltageState()      const { return m_base + "/profile/precharge_voltage/state"; }
    std::string loadDisconnectVoltageState() const { return m_base + "/profile/load_disconnect_voltage/state"; }
    std::string maxChargingCurrentState()    const { return m_base + "/profile/max_charging_current/state"; }

    // Profile command topics (subscribed by controller, published by HA)
    std::string batteryTypeSet()             const { return m_base + "/profile/battery_type/set"; }
    std::string maxVoltageSet()              const { return m_base + "/profile/max_voltage/set"; }
    std::string rechargeVoltageSet()         const { return m_base + "/profile/recharge_voltage/set"; }
    std::string prechargeVoltageSet()        const { return m_base + "/profile/precharge_voltage/set"; }
    std::string loadDisconnectVoltageSet()   const { return m_base + "/profile/load_disconnect_voltage/set"; }
    std::string maxChargingCurrentSet()      const { return m_base + "/profile/max_charging_current/set"; }

    // HomeAssistant MQTT Discovery topic
    // Pattern: homeassistant/{component}/{deviceId}/{objectId}/config
    std::string hassDiscovery(std::string_view component, std::string_view objectId) const
    {
        return "homeassistant/" + std::string{component} + "/" + m_deviceId + "/" + std::string{objectId} + "/config";
    }

    const std::string& deviceId() const { return m_deviceId; }

private:
    std::string m_deviceId;
    std::string m_base;
};

/**
 * @class MqttSolarControllerBridge
 * @brief Domain-specific MQTT bridge for the solar charge controller.
 *
 * Responsibilities:
 *   - Register MQTT subscriptions for all battery profile command topics
 *   - Publish periodic sensor telemetry (PV / battery measurements, charging mode)
 *   - Publish and maintain battery profile state topics (retained)
 *   - Publish HomeAssistant MQTT discovery configs on every (re)connect
 *   - Handle incoming profile change commands: validate → save to NVS → restart
 *
 * Usage:
 *   Call init() once from setup() after MqttClient::init() and BatteryProfileSelector::init().
 *   Call publishTelemetry() periodically from loop() (e.g. every 5 seconds).
 */
class MqttSolarControllerBridge
{
public:
    MqttSolarControllerBridge(MqttClient& mqttClient, BatteryProfileSelector& profileSelector, std::string_view deviceId);

    /**
     * @brief Registers command subscriptions and the onConnect callback.
     *        Discovery and initial profile state are published on every (re)connect.
     */
    void init();

    /**
     * @brief Publish sensor telemetry. Call periodically from loop().
     *
     * @param pvMeas          PV voltage and current measurements.
     * @param battMeas        Battery voltage and current measurements.
     * @param mode            Current battery charging mode.
     * @param mpptControl_pct MPPT control signal in percent (0–100).
     */
    void publishTelemetry(const MeasurementsIf& pvMeas, const MeasurementsIf& battMeas, BatteryManager::Mode  mode, int mpptControl_pct);

private:
    // Called from onConnect — publishes all HA discovery config payloads (retained)
    void publishDiscovery();

    // Called from onConnect and after every successful profile change (retained)
    void publishProfileState();

    // Subscribes all profile command topics with their handlers
    void registerCommands();

    // Command handlers — called when HA publishes to a command topic
    // Integer-valued commands use handleIntCommand() directly; only battery type needs a dedicated handler.
    void onBatteryTypeSet(std::string_view payload);

    // Shared logic: saves profile to NVS, publishes updated state, then restarts ESP
    void saveAndRestart();

    // Helper: handles boilerplate for integer-valued commands
    void handleIntCommand(std::string_view name, std::string_view payload, std::function<BatteryProfileSelector::Result(int)> setter);

    static std::string_view                          batteryModeToString(BatteryManager::Mode mode);
    static std::string_view                          batteryTypeToString(BatteryConfig::BatteryType type);
    static std::optional<BatteryConfig::BatteryType> stringToBatteryType(std::string_view str);

    MqttClient&                     m_mqttClient;
    BatteryProfileSelector&         m_profileSelector;
    MqttSolarControllerTopicBuilder m_topics;

    /// Delay before ESP restart to allow the MQTT publish to reach the broker.
    static constexpr unsigned long RESTART_DELAY_MS{2000};
};
