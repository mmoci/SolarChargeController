#pragma once

#include <PubSubClient.h>
#include <WiFi.h>
#include <string_view>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>

/**
 * @class MqttClient
 * @brief Thin, reusable MQTT transport layer over PubSubClient.
 *
 * Responsibilities (this class only):
 *   - WiFi + MQTT broker connection with automatic reconnection
 *   - Publishing messages with optional broker-side retain
 *   - Subscribing topics with per-topic message callbacks
 *   - Notifying registered listeners on (re)connect
 *
 * Explicitly NOT responsible for:
 *   - Domain knowledge (topics, payloads, JSON formatting)
 *   - Application state (battery mode, profile data)
 *   - WiFi credentials — injected via Config struct
 *
 * Design decisions:
 *   - No publish queue: broker retain flag provides last-value persistence.
 *     State that must survive reconnects is republished in onConnect callbacks.
 *   - Multiple onConnect callbacks (additive): each bridge registers independently
 *     without overwriting others.
 *   - No topic pre-registration: publish() works on any topic at any time.
 */
class MqttClient
{
    public:
    using MessageCallback = std::function<void(std::string_view payload)>;
    using ConnectCallback = std::function<void()>;

    /**
     * @brief All connection parameters injected at construction.
     */
    struct Config
    {
        std::string_view broker;
        int              port{1883};
        std::string_view clientId;
        std::string_view username;
        std::string_view password;
        std::string_view wifiSsid;
        std::string_view wifiPassword;
        std::string_view willTopic;     ///< Availability topic — used for both LWT and online announce
        std::string_view willPayload;   ///< LWT payload published by broker on unexpected disconnect (typically "offline")
        std::string_view onlinePayload; ///< Payload published by client after every successful connect (typically "online")
    };

    explicit MqttClient(const Config& config);

    /**
     * @brief Initialises WiFi (blocking until connected) and configures MQTT broker.
     *        Call once from setup().
     */
    void init();

    /**
     * @brief Drives reconnection logic and incoming message dispatching.
     *        Call every loop() iteration.
     */
    void process();

    /**
     * @brief Publish a message to the broker.
     * @param topic   Full topic string.
     * @param payload Message payload.
     * @param retain  true = broker stores last value for late subscribers.
     *                Use retain=true for state/config, false for high-frequency telemetry.
     */
    void publish(std::string_view topic, std::string_view payload, bool retain = false);

    /**
     * @brief Subscribe to a topic and register a callback for incoming messages.
     *        Safe to call before connect() — subscriptions are replayed on every reconnect.
     * @param topic    Full topic string (including any wildcards).
     * @param callback Invoked with the payload string on each matching message.
     */
    void subscribe(std::string_view topic, MessageCallback callback);

    /**
     * @brief Register a callback to be invoked after every successful (re)connect.
     *        Additive — multiple callers can register without overwriting each other.
     *        Typical use: republish retained state so broker/subscribers stay in sync.
     * @param callback Invoked with no arguments after each successful connect. Should be 
     *                 non-blocking and not throw exceptions.
     */
    void onConnect(ConnectCallback callback);

    bool isConnected();

    private:
    bool connect();
    void setupWifi();
    void subscribeAll();
    void dispatchMessage(char* topic, byte* payload, unsigned int length);

    Config       m_config;
    WiFiClient   m_wifiClient{};
    PubSubClient m_mqttClient{};

    std::vector<ConnectCallback>                      m_connectCallbacks{};
    std::unordered_map<std::string, MessageCallback>  m_subscriptions{};

    static constexpr unsigned long RECONNECT_INTERVAL_MS{5000};
};
