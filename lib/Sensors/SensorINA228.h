#pragma once

#include "SensorINAIf.h"
#include "Config.h"

class SensorINA228 : public SensorINAIf
{
    public:
    SensorINA228(uint16_t deviceAddress, int shunt_mOhm = SensorConfig::SensorINA228::PV_SHUNT_mOhm);

    // Device overrides
    void init() override;
    void update() override;

    private:
    static constexpr int      MAX_CURRENT_A        {20};                       // Maximum expected current in Amperes
    static constexpr double   CURRENT_LSB          {MAX_CURRENT_A / 524288.0}; // Current_LSB = Maximum Expected Current / pow(2, 19) (20-bit register)
    static constexpr float    VOLTAGE_LSB_mV       {0.1953125f};               // 195.3125 µV/LSB
    static constexpr uint16_t CONFIG_DEFAULT       {0x0000};                   // Default configuration value for configuration register
    static constexpr uint16_t ADC_CONFIG_DEFAULT   {0xFB68};                   // Default ADC configuration value
    static constexpr uint16_t DIAG_ALERT_DEFAULT   {0x8000};                   // Default diagnostic alert value
    static constexpr float    BOVL_LSB_mV          {3.125f};                   // BOVL/BUVL threshold register LSB
    static constexpr uint16_t WAKEUP_THRESHOLD_DEFAULT
        {static_cast<uint16_t>(PvArrayConfig::DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV *
        PvArrayConfig::WAKEUP_THRESHOLD_PERCENT / 100 / BOVL_LSB_mV)};

    struct RegisterSize
    {
        static constexpr uint8_t CONFIG     {2}; // Bytes
        static constexpr uint8_t ADC_CONFIG {2}; // Bytes
        static constexpr uint8_t SHUNT_CAL  {2}; // Bytes
        static constexpr uint8_t DIAG_ALERT {2}; // Bytes
        static constexpr uint8_t SOVL       {2}; // Bytes
        static constexpr uint8_t SUVL       {2}; // Bytes
        static constexpr uint8_t BOVL       {2}; // Bytes
        static constexpr uint8_t BUVL       {2}; // Bytes
        static constexpr uint8_t V_SHUNT    {3}; // Bytes
        static constexpr uint8_t V_BUS      {3}; // Bytes
        static constexpr uint8_t POWER      {3}; // Bytes
        static constexpr uint8_t CURRENT    {3}; // Bytes
        static constexpr uint8_t DIE_TEMP   {3}; // Bytes
        static constexpr uint8_t ENERGY     {5}; // Bytes
        static constexpr uint8_t CHARGE     {5}; // Bytes
    };

    enum class Registers
    {
        CONFIG                 = 0x00,
        ADC_CONFIG             = 0x01,
        SHUNT_CAL              = 0x02,
        SHUNT_TEMP_COEFFICIENT = 0x03,
        VOLTAGE_SHUNT          = 0x04,
        VOLTAGE_BUS            = 0x05,
        DIE_TEMPERATURE        = 0x06,
        CURRENT                = 0x07,
        POWER                  = 0x08,
        ENERGY                 = 0x09,
        CHARGE                 = 0x0A,
        DIAG_ALERT             = 0x0B,
        SOVL                   = 0x0C,
        SUVL                   = 0x0D,
        BOVL                   = 0x0E,
        BUVL                   = 0x0F,
        TEMP_LIMIT             = 0x10,
        PWR_LIMIT              = 0x11,
        MANUFACTURER_ID        = 0x3E,
        DEVICE_ID              = 0x3F
    };

    void setShuntCalibrationRegister() override;
    void setConfigurationRegister(uint16_t configValue = CONFIG_DEFAULT) override;
    void setADCConfigurationRegister(uint16_t adcConfigValue = ADC_CONFIG_DEFAULT);
    void setDiagnosticAlertRegister(uint16_t diagAlertValue = DIAG_ALERT_DEFAULT);
    void setOvervoltageThreshold(uint16_t ovThreshold = WAKEUP_THRESHOLD_DEFAULT);

    int m_shunt_mOhm{};
};