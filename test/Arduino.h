#pragma once

// Mock Arduino definitions for testing with native platform
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// Type definitions
typedef unsigned char byte;
typedef unsigned long ul;

// Mock String class (Arduino String)
class String : public std::string
{
public:
    String() : std::string() {}
    String(const char* str) : std::string(str) {}
    String(const std::string& str) : std::string(str) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(double val) : std::string(std::to_string(val)) {}
    
    // Concatenation operators
    String operator+(const char* str) const
    {
        return String(std::string(*this) + str);
    }
    
    String operator+(const String& str) const
    {
        return String(std::string(*this) + std::string(str));
    }
    
    String operator+(int val) const
    {
        return String(std::string(*this) + std::to_string(val));
    }
    
    String operator+(long val) const
    {
        return String(std::string(*this) + std::to_string(val));
    }
};

// Friend function for char* + String
inline String operator+(const char* lhs, const String& rhs)
{
    return String(std::string(lhs) + std::string(rhs));
}

// Friend function for int + String  
inline String operator+(int lhs, const String& rhs)
{
    return String(std::to_string(lhs) + std::string(rhs));
}

// Mock Serial class
class SerialClass
{
public:
    void begin(unsigned long baud) {}
    void print(const char* str) {}
    void println(const char* str) {}
    void print(int val) {}
    void println(int val) {}
    void print(const String& str) {}
    void println(const String& str) {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t* buf, size_t size) { return 0; }
    size_t readBytes(uint8_t* buf, size_t size) { return 0; }
    template<typename... Args>
    void printf(const char* /*fmt*/, Args... /*args*/) {}
};

// Mock Serial class for second hardware serial
class Serial2Class : public SerialClass
{
};

extern SerialClass Serial;
extern Serial2Class Serial2;

// Mock Wire (I2C) class
class WireClass
{
public:
    void begin() {}
    void begin(int sda, int scl) {}
};

extern WireClass Wire;

// Mock millis function
unsigned long millis();

// Helper functions for test time control
void advance_millis(unsigned long ms);

// Mock LEDC (PWM for ESP32) functions
inline void ledcSetup(int channel, int frequency, int resolution) {}
inline void ledcAttachPin(int pin, int channel) {}
inline void ledcWrite(int channel, int pwm) {}
inline void ledcDetachPin(int pin) {}
inline void analogWrite(int pin, int value) {}
void reset_millis();

// Utility macros - use constrain instead of min/max to avoid conflicts with std
#define constrain(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

extern SerialClass Serial;
extern Serial2Class Serial2;
extern WireClass Wire;

// Global milliseconds counter for tests (can be mocked)
extern unsigned long g_millis_counter;
