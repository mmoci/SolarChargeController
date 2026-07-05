#pragma once

#include <Arduino.h>
#include <sstream>
#include <iomanip>
#include <array>
#include <queue>
#include <vector>
#include <optional>

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

template <typename T, size_t MaxSize>
class CircularBuffer
{
    private:
    std::array<std::optional<T>, MaxSize> m_buffer{};
    std::size_t m_index{0};

    public:
    void add(const T& item)
    {
        m_buffer[m_index] = item;
        m_index = (m_index + 1) % MaxSize;
    }

    void add(T&& item)
    {
        m_buffer[m_index] = std::move(item);
        m_index = (m_index + 1) % MaxSize;
    }

    void clear()
    {
        for (auto& element : m_buffer)
            element.reset();
        m_index = 0;
    }

    bool empty() const
    {
        return std::all_of(m_buffer.begin(), m_buffer.end(), [](const std::optional<T>& element) { return !element.has_value(); });
    }

    std::size_t size() const
    {
        return std::count_if(m_buffer.begin(), m_buffer.end(), [](const std::optional<T>& element) { return element.has_value(); });
    }

    std::optional<T> getMinElement() const
    {
        if (empty())
            return std::nullopt;
        return *std::min_element(m_buffer.begin(), m_buffer.end(), [](const std::optional<T>& a, const std::optional<T>& b)
        {
            if (!a.has_value()) return false; // Treat empty slots as greater than any value
            if (!b.has_value()) return true;
            return a.value() < b.value();
        });
    }

    std::optional<T> getMaxElement() const
    {
        if (empty())
            return std::nullopt;
        return *std::max_element(m_buffer.begin(), m_buffer.end(), [](const std::optional<T>& a, const std::optional<T>& b)
        {
            if (!a.has_value()) return true; // Treat empty slots as less than any value
            if (!b.has_value()) return false;
            return a.value() < b.value();
        });
    }

    std::vector<T> getAll() const
    {
        std::vector<T> items;
        for (const auto& element : m_buffer)
        {
            if (element.has_value())
                items.push_back(*element);
        }
        return items;
    }

    std::optional<T> getLastElement() const
    {
        std::size_t lastIndex = (m_index - 1 + MaxSize) % MaxSize;
        if (!m_buffer[lastIndex].has_value())
        {
            return std::nullopt;
        }
        return m_buffer[lastIndex];
    }

    const std::optional<T>& operator[](std::size_t index) const
    {
        static const std::optional<T> dummy{};
        if (index >= MaxSize)
            return dummy;
        return m_buffer[index];
    }
};

struct Measurements
{
    int voltage_mV{};
    int current_mA{};
    std::optional<int> openCircuitVoltage_mV{}; // Optional open-circuit voltage measurement, not all sensors provide this
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