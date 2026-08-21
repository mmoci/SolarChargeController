#pragma once
#include <Arduino.h>
#include "BatteryProfile.h"

namespace SensorConfig
{
    namespace SensorINA226
    {
        // INA226 max differential input = ±81.92mV.
        // Shunt must keep voltage drop <= 81.92mV at maximum expected current.
        // e.g. 10mΩ -> saturation at 8.19A  |  5mΩ -> saturation at 16.38A
        // Adjust to match the actual shunt resistor populated on the board.
        constexpr int PV_SHUNT_mOhm      = 10; // mΩ
        constexpr int BATTERY_SHUNT_mOhm = 10; // mΩ
    }

    namespace SensorINA228
    {
        constexpr int PV_SHUNT_mOhm      = 2; // mΩ
        constexpr int BATTERY_SHUNT_mOhm = 2; // mΩ
    }

    // INA226 I2C addresses are set by A0/A1 pins (0x40–0x4F).
    // A0=GND, A1=GND -> 0x40   |   A0=VS, A1=GND -> 0x41
    // Adjust to match the hardware pin strapping on each sensor board.
    static constexpr uint16_t PV_SENSOR_DEVICE_ADDRESS      {0x40};
    static constexpr uint16_t BATTERY_SENSOR_DEVICE_ADDRESS {0x41};
}

namespace ChargeControllerConfig
{
    static constexpr long PV_POWER_THRESHOLD           {1000};  // 1W, When PV power is less or equal to 1W consider PV power unavailable (i.e. to small for charging)
    static constexpr int  PV_INPUT_HEADROOM_MV         {1000};  // 1V — Vin must exceed Vbatt by at least this to indicate a panel is present. Below this the DPS buck converter cannot conduct (Vin < Vout), so no panel = no charging.
    static constexpr long PV_POWER_UNAVAILABLE_TIMEOUT {10000}; // 10sec, When PV power is unavailable for 10sec, set isPvAvailable to false
    static constexpr unsigned long STALE_LOG_INTERVAL  {10000}; // 10sec, Minimum interval between repeated stale-measurement warnings

    static constexpr double Kp {1.0}; // Proportional gain for clampLimitPI — how aggressively to reduce MPPT control in response to over-limit measurements. Higher Kp reduces overshoot but can cause instability; lower Kp is more stable but allows larger excursions above the limit. Recommend starting with a modest value (e.g. 0.5 to 2.0) and tuning based on observed performance.
    static constexpr double Ki {0.01}; // Integral gain for clampLimitPI — how aggressively to accumulate error over time. Higher Ki eliminates steady-state error but can cause overshoot and instability; lower Ki is more stable but allows sustained excursions above the limit. Recommend starting with a small value (e.g. 0.001 to 0.1) and tuning based on observed performance.
    static constexpr long   MAX_INTEGRAL_ERROR {500};

    static constexpr int PANEL_PRESENT_MIN_OUTPUT_VOLTAGE_mV {5000}; // Floor for Vout comparison when output is unloaded or at startup
    static constexpr int PANEL_PRESENT_VIN_HEADROOM_mV       {1000}; // Vin must exceed max(Vout, floor) by this margin to consider panel connected
    static constexpr int PANEL_PRESENT_MIN_OUTPUT_POWER_mW   {1000}; // Minimum output power to consider panel present

    static constexpr unsigned long long DEFAULT_DEEP_SLEEP_DURATION_S {600}; // Default deep sleep duration in seconds (10 minutes)
}

namespace PvArrayConfig
{
    static constexpr int DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV {49000};
    static constexpr int VOLTAGE_COLLAPSE_THRESHOLD_PERCENT {78};
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