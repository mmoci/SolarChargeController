#include "SensorINA228.h"
#include "Logger.h"

static constexpr char TAG[] = "SensorINA228";

SensorINA228::SensorINA228(uint16_t deviceAddress, int shunt_mOhm) : 
    SensorINAIf(deviceAddress), 
    m_shunt_mOhm{shunt_mOhm}
{}

void SensorINA228::init()
{
    m_isConnected = i2cDevicePresent();

    if(!m_isConnected)
    {
        ESP_LOGE(TAG, "Device is not connected!");
        return;
    }

    setShuntCalibrationRegister();
    setConfigurationRegister(CONFIG_DEFAULT);
}

void SensorINA228::update()
{
    if (!m_isConnected) return;

    uint8_t bufferVoltage[RegisterSize::V_BUS]{}; // 3 bytes for bus voltage

    // Read bus voltage from register
    if(!readRegisterBytes(static_cast<uint16_t>(Registers::VOLTAGE_BUS), bufferVoltage, RegisterSize::V_BUS))
    {
        ESP_LOGE(TAG, "Reading register bytes for bus voltage failed, exiting update()!");
        return;
    }

    // Convert the 3-byte buffer to a 24-bit unsigned integer
    uint32_t rawBusVoltage = (static_cast<uint32_t>(bufferVoltage[0]) << 16) |
                             (static_cast<uint32_t>(bufferVoltage[1]) << 8) |
                             static_cast<uint32_t>(bufferVoltage[2]);

    uint32_t raw20bitBusVoltage = rawBusVoltage >> 4; // Shift right by 4 to get the 20-bit value

    // Convert raw bus voltage to millivolts using the LSB value
    m_voltage_mV = static_cast<int>(raw20bitBusVoltage * VOLTAGE_LSB_mV);

    uint8_t bufferCurrent[RegisterSize::CURRENT]{}; // 3 bytes for current

    // Read current from register
    if(!readRegisterBytes(static_cast<uint16_t>(Registers::CURRENT), bufferCurrent, RegisterSize::CURRENT))
    {
        ESP_LOGE(TAG, "Reading register bytes for current failed, exiting update()!");
        return;
    }

    // Convert the 3-byte buffer to a 24-bit signed integer
    int32_t rawCurrent = (static_cast<int32_t>(bufferCurrent[0]) << 16) |
                         (static_cast<int32_t>(bufferCurrent[1]) << 8) |
                         static_cast<int32_t>(bufferCurrent[2]);

    if (rawCurrent & 0x800000) rawCurrent |= 0xFF000000; // Sign-extend if negative
    
    int32_t raw20bitCurrent = rawCurrent >> 4; // Shift right by 4 to get the 20-bit value

    // Convert raw current to milliamperes using the LSB value
    m_current_mA = static_cast<int>(raw20bitCurrent * CURRENT_LSB * 1000); // Convert A to mA

    m_lastUpdateTime = millis();
}

void SensorINA228::setConfigurationRegister(uint16_t configValue)
{
    uint8_t data[RegisterSize::CONFIG]{}; 
    data[0] = static_cast<uint8_t>((configValue >> 8) & 0xFF); // High byte
    data[1] = static_cast<uint8_t>(configValue & 0xFF);        // Low byte

    writeRegisterBytes(static_cast<uint16_t>(Registers::CONFIG), data, RegisterSize::CONFIG);
}

void SensorINA228::setADCConfigurationRegister(uint16_t adcConfigValue)
{
    uint8_t data[RegisterSize::ADC_CONFIG]{};
    data[0] = static_cast<uint8_t>((adcConfigValue >> 8) & 0xFF); // High byte
    data[1] = static_cast<uint8_t>(adcConfigValue & 0xFF);        // Low byte

    writeRegisterBytes(static_cast<uint16_t>(Registers::ADC_CONFIG), data, RegisterSize::ADC_CONFIG);
}

void SensorINA228::setShuntCalibrationRegister()
{
    // INA228 datasheet formula:
    uint16_t calibration = static_cast<uint16_t>(std::lround(13107.2e6 * CURRENT_LSB * (m_shunt_mOhm / 1000.0)));

    uint8_t data[RegisterSize::SHUNT_CAL]{};
    data[0] = static_cast<uint8_t>((calibration >> 8) & 0xFF); // High byte
    data[1] = static_cast<uint8_t>(calibration & 0xFF);        // Low byte

    writeRegisterBytes(static_cast<uint16_t>(Registers::SHUNT_CAL), data, RegisterSize::SHUNT_CAL);
}