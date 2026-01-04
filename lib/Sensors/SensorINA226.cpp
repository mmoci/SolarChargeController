#include "SensorINA226.h"

SensorINA226::SensorINA226(uint16_t deviceAddress, int shunt_mOhm) : 
    SensorI2C(deviceAddress), m_shunt_mOhm{shunt_mOhm}
{}

void SensorINA226::init()
{
    m_isConnected = i2cDevicePresent();

    if(!m_isConnected)
    {
        Serial.println("[SensorINA226]: Device is not connected!");
        return;
    }

    configureCalibration();
    setConfiguration();
}

void SensorINA226::configureCalibration()
{
    // INA226 datasheet formula: 
    // External shunt used to develop the differential voltage across the input pins.
    // CAL(calibration) = 0.00512 / (Current_LSB × RSHUNT) where 0.00512 is an internal fixed value used to verify scaling is maintained properly
    // Current_LSB = Maximum Expected Current / pow(2, 15) (16 bits register)
    uint16_t calibration = static_cast<uint16_t>(std::lround(0.00512 / ((CURRENT_LSB_mA / 1000.0) * (m_shunt_mOhm / 1000.0))));

    uint8_t data[REGISTERS_SIZE_IN_BYTES]{};
    data[0] = static_cast<uint8_t>((calibration >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(calibration & 0xFF);

    writeRegisterBytes(static_cast<uint16_t>(Registers::Calibration), data, REGISTERS_SIZE_IN_BYTES);
}

void SensorINA226::setConfiguration()
{
    uint8_t data[REGISTERS_SIZE_IN_BYTES]{};
    data[0] = static_cast<uint8_t>((CONFIG_DEFAULT >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(CONFIG_DEFAULT & 0xFF);

    writeRegisterBytes(static_cast<uint16_t>(Registers::Config), data, REGISTERS_SIZE_IN_BYTES);
}

void SensorINA226::update()
{
    if (!m_isConnected) return;

    uint8_t buffer[REGISTERS_SIZE_IN_BYTES]{};
    
    // Read bus voltage from register
    if(!readRegisterBytes(static_cast<uint16_t>(Registers::BusVoltage), buffer, REGISTERS_SIZE_IN_BYTES))
    {
        Serial.println("[SensorINA226]: Reading register bytes for bus voltage failed, exiting update()!");
        return;
    }
              
    uint16_t rawBusVoltage{static_cast<uint16_t>(buffer[0] << 8 | buffer[1])}; //(buffer[0] << 8) && buffer[1] small types, implicit promotion to int during aritmetic operations than explicitly cast.
    m_voltage_mV = std::lround(rawBusVoltage * VOLTAGE_LSB_mV); // We accept narrowing conversion as it is safe

    // Read current from register
    if(!readRegisterBytes(static_cast<uint16_t>(Registers::Current), buffer, REGISTERS_SIZE_IN_BYTES))
    {
        Serial.println("[SensorINA226]: Reading register bytes for current failed, exiting update()!");
        return;
    }
    int16_t rawCurrent{static_cast<int16_t>(buffer[0] << 8 | buffer[1])};
    m_current_mA = rawCurrent * CURRENT_LSB_mA;
}