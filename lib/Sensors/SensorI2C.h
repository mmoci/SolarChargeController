#pragma once

#include "Sensor.h"
#include <Wire.h>

class SensorI2C
{
    protected:
    std::uint16_t m_deviceAddress{};
    bool m_isConnected{};

    std::uint8_t writeBytes(const std::uint8_t* data, std::size_t nrOfBytes, bool stop = true);
    std::uint8_t writeRegisterBytes(std::uint16_t registerAddress,  const std::uint8_t* data, std::size_t nrOfBytes, bool stop = false);
    bool readBytes(std::uint8_t* buffer, std::size_t nrOfBytes, bool stop = true);
    bool readRegisterBytes(std::uint16_t registerAddress, std::uint8_t* buffer, std::size_t nrOfBytes, bool stop = false);
    bool i2cDevicePresent();

    public:
    SensorI2C(std::uint16_t address) : m_deviceAddress{address} 
    {}
};