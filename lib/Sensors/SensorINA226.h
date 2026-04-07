#pragma once

#include "Config.h"
#include "Device.h"
#include "SensorI2C.h"
#include "MeasurementsIf.h"

class SensorINA226 : public Device, public SensorI2C, public MeasurementsIf
{
    public:
    SensorINA226(uint16_t deviceAddress, int shunt_mOhm = SensorConfig::SensorINA226::PV_SHUNT_mOhm);

    /**
     * @brief Provides basic setup for the sensor during Arduino setup phase.
     */
    void init() override;

    /**
     * @brief Provides sensor update during Arduino loop execution.
     */
    void update() override;

    /**
     * @brief Get the Current
     * 
     * @return int Returns current value in [mA]
     */
    int getCurrent_mA() const override {return m_current_mA;};

    /**
     * @brief Get the Bus Voltage
     * 
     * @return int Returns Bus Voltage values in [mV]
     */
    int getVoltage_mV() const override {return m_voltage_mV;}

    /**
     * @brief Returns false if the device is not connected or if no successful
     *        I2C read has completed within STALE_TIMEOUT_MS.
     */
    bool isMeasurementValid() const override
    {
        if (!m_isConnected) return false;
        return (millis() - m_lastUpdateTime) < STALE_TIMEOUT_MS;
    }

    /**
     * @brief Returns milliseconds elapsed since the last successful I2C read.
     */
    unsigned long lastTimeUpdated() const override
    {
        return millis() - m_lastUpdateTime;
    }

    private:
    /**
     * @brief Represents current value per 1bit. 
     *        Current register is signed 16-bit.
     *        Possible representation [0 - 32767] mA (signed).
     */
    static constexpr int CURRENT_LSB_mA {1}; // mA
    
    /**
     * @brief Represents bus voltage value per 1bit. 
     *        Bus voltage register is unsigned 16-bit.
     *        Possible representation [0 - 65535] mV (unsigned).
     */
    static constexpr float VOLTAGE_LSB_mV {1.25}; // mV

    /**
     * @brief Default config must enable shunt + bus voltage continuous measurement.
     *        Conversion times and averaging can be set to reasonable defaults (e.g., 1ms per conversion).
     *        For now, we can define a compile-time constant in SensorINA226 as:
     */
    static constexpr uint16_t CONFIG_DEFAULT {0x4127}; // Continuous shunt+bus, 1ms conv time

    /**
     * @brief INA226 registers are 16-bit -> 2 bytes.
     */
    static constexpr uint8_t REGISTERS_SIZE_IN_BYTES {2}; // Bytes

    /**
     * @brief INA226 register addresses according to datasheet
     */
    enum class Registers
    {
        Config      = 0x00,
        BusVoltage  = 0x02,
        Current     = 0x04,
        Calibration = 0x05
    };

    int m_voltage_mV{};
    int m_current_mA{};
    int m_shunt_mOhm{};
    unsigned long m_lastUpdateTime{};

    // If no successful read arrives within this window the measurement is
    // considered stale. INA226 reads every ~6ms so 100ms gives ~16x headroom
    // for transient I2C hiccups before the controller reacts.
    static constexpr unsigned long STALE_TIMEOUT_MS{100};

    void configureCalibration();
    void setConfiguration();
};