#pragma once

#include "BatteryProfile.h"
#include "Config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string_view>

class BatteryProfileSelector
{
    public:

    // Result type for profile operations
    enum class Result
    {
        SUCCESS,
        NVS_LOAD_ERROR,
        NVS_SAVE_ERROR,
        VALIDATION_ERROR,
        PROFILE_TYPE_NOT_FOUND
    };

    // NVS keys for profile parameters
    enum class NvsKey
    {
        BATTERY_TYPE,
        BATTERY_PROFILE,
        MAX_VOLTAGE,
        RECHARGE_VOLTAGE,
        PRECHARGE_VOLTAGE,
        LOAD_DISCONNECT_VOLTAGE,
        MAX_CHARGING_CURRENT
    };
    
    BatteryProfileSelector() = default;

    void init();

    // Returns currently selected battery type and profile
    const BatteryConfig::BatteryType getCurrentType() const {return m_currentType;}
    const BatteryProfile& getCurrentProfile() const {return m_currentProfile;}

    // Profile management
    Result saveProfileToNVS();
    Result loadProfileFromNVS();

    // Validation
    Result validateProfile(const BatteryProfile& profile);

    // Modification
    Result setProfileType(BatteryConfig::BatteryType type);
    Result setMaxVoltage(int maxVoltage_mV);
    Result setRechargeVoltage(int rechargeVoltage_mV);
    Result setPrechargeVoltage(int prechargeVoltage_mV);
    Result setLoadDisconnectVoltage(int loadDisconnectVoltage_mV);
    Result setMaxChargingCurrent(int maxChargingCurrent_mA);

    // Utility
    void printCurrentProfile() const;

    private:
    const std::string_view nvsKeyToString(NvsKey key) const;

    BatteryProfile m_currentProfile{};
    BatteryConfig::BatteryType m_currentType{};
    nvs_handle_t m_nvsHandle{};

    static constexpr float VOLTAGE_OVERRIDE_UPPER_MARGIN = 1.1f;  // +10% max
    static constexpr float VOLTAGE_OVERRIDE_LOWER_MARGIN = 0.9f;  // -10% min
    static constexpr int   MAX_CHARGING_CURRENT = 10000; // 10A max for safety
};