#include "MqttClient.h"
#include <Arduino.h>

MqttClient::MqttClient(const Config& config) : m_config{config}, m_mqttClient{m_wifiClient}
{}

void MqttClient::init()
{
    setupWifi();

    m_mqttClient.setServer(m_config.broker.data(), m_config.port);
    m_mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
        dispatchMessage(topic, payload, length);
    });
}

void MqttClient::process()
{
    static unsigned long lastWifiAttempt{0};
    static unsigned long lastReconnectAttempt{0};
    static bool wifiWasConnected{false};

    // Step 1: ensure WiFi is up — without it MQTT cannot connect.
    if (WiFi.status() != WL_CONNECTED)
    {
        wifiWasConnected = false;
        const unsigned long now{millis()};
        if (now - lastWifiAttempt >= WIFI_RECONNECT_INTERVAL_MS)
        {
            lastWifiAttempt = now;
            Serial.println("[MqttClient] WiFi disconnected — attempting reconnect...");
            WiFi.reconnect();
        }
        return; // Charging continues unaffected; MQTT waits for WiFi
    }

    if (!wifiWasConnected)
    {
        wifiWasConnected = true;
        Serial.printf("[MqttClient] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    }

    // Step 2: ensure MQTT broker connection is up.
    if (!m_mqttClient.connected())
    {
        const unsigned long now{millis()};
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL_MS)
        {
            lastReconnectAttempt = now;
            if (connect())
                lastReconnectAttempt = 0;
        }
        return;
    }

    // Step 3: service incoming messages.
    m_mqttClient.loop();
}

void MqttClient::publish(std::string_view topic, std::string_view payload, bool retain)
{
    if (!m_mqttClient.connected())
        return;

    bool isPublished = m_mqttClient.publish(topic.data(), payload.data(), retain);
    if (!isPublished)
        Serial.printf("[MqttClient] publish() failed for topic: %s\n", topic.data());
}

void MqttClient::subscribe(std::string_view topic, MessageCallback callback)
{
    m_subscriptions.emplace(std::string{topic}, std::move(callback));

    // If already connected, subscribe immediately; otherwise subscribeAll() handles it on connect.
    if (m_mqttClient.connected())
    {
        m_mqttClient.subscribe(topic.data());
        Serial.printf("[MqttClient] Subscribed: %s\n", topic.data());
    }
}

void MqttClient::onConnect(ConnectCallback callback)
{
    m_connectCallbacks.push_back(std::move(callback));
}

bool MqttClient::isConnected()
{
    return m_mqttClient.connected();
}


/******************
* Private methods *  
*******************/

bool MqttClient::connect()
{
    Serial.println("[MqttClient] Attempting MQTT connection...");

    bool isConnected = m_mqttClient.connect(
        m_config.clientId.data(),
        m_config.username.data(),
        m_config.password.data(),
        m_config.willTopic.data(),
        /*willQoS*/  0,
        /*willRetain*/ true,
        m_config.willPayload.data()
    );

    if (!isConnected)
    {
        Serial.printf("[MqttClient] Connection failed, PubSubClient state: %d\n", m_mqttClient.state());
        return false;
    }

    Serial.println("[MqttClient] Connected to broker.");

    // Publish "online" status if configured (willTopic is used for both LWT and online announce)
    if (!m_config.onlinePayload.empty())
        m_mqttClient.publish(m_config.willTopic.data(), m_config.onlinePayload.data(), /*retain=*/true);

    // Subscribe to all topics after connecting (including any that were added while disconnected)
    subscribeAll();

    // Invoke all registered onConnect callbacks
    for (auto& connectCb : m_connectCallbacks)
        connectCb();

    return true;
}

bool MqttClient::setupWifi()
{
    Serial.printf("[MqttClient] Connecting to WiFi: %s\n", m_config.wifiSsid.data());

    WiFi.mode(WIFI_STA);

    if (m_config.staticIp)
        WiFi.config(m_config.staticIp, m_config.gateway, m_config.subnet, m_config.dns1, m_config.dns2);

    Serial.flush(); // Drain TX buffer before radio power-up causes voltage dip
    WiFi.begin(m_config.wifiSsid.data(), m_config.wifiPassword.data());

    if (m_config.staticIp)
    {
        // Static IP: DHCP skipped, connects in ~300ms — safe to block briefly
        const unsigned long deadline{millis() + WIFI_CONNECT_TIMEOUT_MS};
        while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        {
            delay(500);
            Serial.print(".");
            Serial.flush();
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("\n[MqttClient] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.println("\n[MqttClient] WiFi unavailable — continuing in standalone mode.");
        return false;
    }

    // Dynamic IP: non-blocking — process() polls and reconnects
    return false;
}

void MqttClient::subscribeAll()
{
    for (const auto& [topic, callback] : m_subscriptions)
        m_mqttClient.subscribe(topic.c_str());
}

void MqttClient::dispatchMessage(char* topic, byte* payload, unsigned int length)
{
    std::string_view strTopic{topic};
    std::string_view strPayload{reinterpret_cast<char*>(payload), length};

    Serial.printf("[MqttClient] Received [%s]: %.*s\n", topic, (int)length, (char*)payload);

    auto it = m_subscriptions.find(std::string{strTopic});
    if (it != m_subscriptions.end())
        it->second(strPayload);
    else
        Serial.printf("[MqttClient] No handler for topic: %s\n", topic);
}
