#pragma once

#include <Arduino.h>
#include <sstream>
#include <iomanip>

/** Simple timer utility class */
class Timer
{
    private:
    unsigned long m_start{};
    unsigned long m_duration{};
    bool          m_active{};

    public:
    void reset()
    {
        m_start    = 0;
        m_duration = 0;
        m_active   = false;
    }

    void trigger()
    {
        m_start    = millis();
        m_duration = 0;
        m_active   = true;
    }

    void update()
    {
        if (!m_active)
        {
            m_duration = 0;
            return;
        }
        m_duration = millis() - m_start;
    }

    bool active() const { return m_active; }

    unsigned long getDuration() const { return m_duration; }
};

struct Measurements
{
    int voltage_mV{};
    int current_mA{};
};

inline bool parseIntSafe(std::string_view str, int& out)
{
    if (str.empty()) return false;
    // Construct null-terminated string — string_view::data() is NOT guaranteed null-terminated,
    // and strtol requires a null-terminated C string. std::stol is not used because exceptions
    // are disabled in the ESP32 Arduino framework (-fno-exceptions), making throws call terminate().
    const std::string s{str};
    char* end{};
    long val = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;   // no digits consumed
    out = static_cast<int>(val);
    return true;
}

inline std::string floatToString(float value, int precision = 2)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}