#include "SensorI2C.h"

std::uint8_t SensorI2C::writeBytes(const std::uint8_t* data, std::size_t nrOfBytes, bool stop)
{
    Wire.beginTransmission(m_deviceAddress);
    
    Wire.write(data, nrOfBytes);
    
    return Wire.endTransmission(stop);
}

std::uint8_t SensorI2C::writeRegisterBytes(std::uint16_t registerAddress,  const std::uint8_t* data, std::size_t nrOfBytes, bool stop)
{
    Wire.beginTransmission(m_deviceAddress);

    // Write address - support for 2 bytes addresses
    Wire.write(static_cast<uint8_t>(registerAddress >> 8));   // MSB
    Wire.write(static_cast<uint8_t>(registerAddress & 0xFF)); // LSB

    // Write data
    Wire.write(data, nrOfBytes);

    return Wire.endTransmission(stop);
}

bool SensorI2C::readBytes(std::uint8_t* buffer, std::size_t nrOfBytes, bool stop)
{
    Wire.requestFrom(m_deviceAddress, nrOfBytes, stop);

    if(Wire.available() >= nrOfBytes)
    {
        Wire.readBytes(buffer, nrOfBytes);
        return true;
    }

    return false;
}

bool SensorI2C::readRegisterBytes(std::uint16_t registerAddress, std::uint8_t* buffer, std::size_t nrOfBytes, bool stop)
{
    Wire.beginTransmission(m_deviceAddress);

    // Write address - support for 2 bytes addresses
    Wire.write(static_cast<uint8_t>(registerAddress >> 8));   // MSB
    Wire.write(static_cast<uint8_t>(registerAddress & 0xFF)); // LSB

    int err {Wire.endTransmission(stop)};
    
    if (err != 0) return false;

    Wire.requestFrom(m_deviceAddress, nrOfBytes, stop);
    if(Wire.available() >= nrOfBytes)
    {
        Wire.readBytes(buffer, nrOfBytes);
        return true;
    }

    return false;
}

bool SensorI2C::i2cDevicePresent()
{
    Wire.beginTransmission(m_deviceAddress);
    return (Wire.endTransmission() == 0);
}