#pragma once
#include <Arduino.h>
#include "BatteryProfile.h"

namespace SensorConfig
{
    namespace SensorINA226
    {
        constexpr int PV_SHUNT_mOhm = 100; //mOhm
    }

    static constexpr uint16_t PV_SENSOR_DEVICE_ADDRESS {0x12};
    static constexpr uint16_t BATTERY_SENSOR_DEVICE_ADDRESS {0x13};
}

namespace ChargeControllerConfig
{
    static constexpr long PV_POWER_THRESHOLD {1000}; // 1W, When PV power is less or equal to 1W consider PV power unavailable (i.e. to small for charging)
    static constexpr long PV_POWER_UNAVAILABLE_TIMEOUT {10000}; // 10sec, When PV power is unavailable for 10sec, set isChargingAvailable to false

    static constexpr double Kp {1.0};
    static constexpr double Ki {0.01};
    static constexpr long   MAX_INTEGRAL_ERROR {500};
    
    #ifdef DPS_DC_CONVERTER
        static constexpr int MAX_CONTROL_SOFT_STEP {1};  // Conservative for Modbus latency
    #else
        static constexpr int MAX_CONTROL_SOFT_STEP {5};  // Aggressive for PWM
    #endif
}

namespace BatteryConfig
{
    enum class BatteryType 
    { 
        LIION_3S, 
        LIION_4S, 
        LIFEPO4_4S, 
        CUSTOM 
    };

    static constexpr BatteryProfile LI_ION_3S_DEFAULT
    {
        .chemistry = BatteryProfile::BatteryChemistry::LiIon,
        .maxVoltage_mV = 12600,
        .rechargeVoltage_mV = 12400,
        .prechargeVoltage_mV = 9600,
        .minSafeVoltage_mV = 9000,
        .loadDisconnectVoltage_mV = 9600,
        .maxChargingCurrent_mA = 10000,
        .cutoffCurrent_mA = 100,
        .prechargeCurrent_mA = 500,
        .idleCurrent_mA = 100
    };

    static constexpr BatteryProfile LI_ION_4S_DEFAULT
    {
        .chemistry = BatteryProfile::BatteryChemistry::LiIon,
        .maxVoltage_mV = 16800,
        .rechargeVoltage_mV = 16500,
        .prechargeVoltage_mV = 12800,
        .minSafeVoltage_mV = 12000,
        .loadDisconnectVoltage_mV = 12800,
        .maxChargingCurrent_mA = 10000,
        .cutoffCurrent_mA = 100,
        .prechargeCurrent_mA = 500,
        .idleCurrent_mA = 100
    };

    static constexpr BatteryProfile LIFEPO4_4S_DEFAULT
    {
        .chemistry = BatteryProfile::BatteryChemistry::LiFePO4,
        .maxVoltage_mV = 14000,
        .rechargeVoltage_mV = 13800,
        .prechargeVoltage_mV = 10000,
        .minSafeVoltage_mV = 9000,
        .loadDisconnectVoltage_mV = 10000,
        .maxChargingCurrent_mA = 10000,
        .cutoffCurrent_mA = 100,
        .prechargeCurrent_mA = 500,
        .idleCurrent_mA = 100
    };

    static constexpr BatteryProfile CUSTOM_DEFAULT
    {
        .chemistry = BatteryProfile::BatteryChemistry::LiIon,
        .maxVoltage_mV = 16800,
        .rechargeVoltage_mV = 16500,
        .prechargeVoltage_mV = 12800,
        .minSafeVoltage_mV = 12000,
        .loadDisconnectVoltage_mV = 12800,
        .maxChargingCurrent_mA = 10000,
        .cutoffCurrent_mA = 100,
        .prechargeCurrent_mA = 500,
        .idleCurrent_mA = 100
    };

    static const BatteryProfile& getDefaultBatteryProfile(BatteryType type)
    {
        static const BatteryProfile batteryProfiles[] = {LI_ION_3S_DEFAULT, LI_ION_4S_DEFAULT, LIFEPO4_4S_DEFAULT, CUSTOM_DEFAULT};
        return batteryProfiles[static_cast<std::size_t>(type)];
    }

}