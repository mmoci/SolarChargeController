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
    static constexpr int    MAX_CONTROL_SOFT_STEP {5};
}

namespace BatteryConfig
{
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
}