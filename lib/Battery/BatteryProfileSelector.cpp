#include "BatteryProfileSelector.h"

void BatteryProfileSelector::init()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        // NVS partition was truncated and needs to be erased
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) 
    {
        Serial.printf("[BatteryProfileSelector] ERROR: Failed to initialize NVS! Error code: %d\n", err);
        setProfileType(BatteryConfig::BatteryType::LIION_3S);
        return;
    }

    // Open NVS handle for battery namespace
    err = nvs_open("battery", NVS_READWRITE, &m_nvsHandle);
    if (err != ESP_OK) 
    {
        Serial.printf("[BatteryProfileSelector] ERROR: Failed to open NVS handle! Error code: %d\n", err);
        return;
    }

    // Check for old blob format and migrate
    size_t blobSize{};
    if (nvs_get_blob(m_nvsHandle, nvsKeyToString(NvsKey::BATTERY_PROFILE).data(), nullptr, &blobSize) == ESP_OK)
    {
        Serial.println("[BatteryProfileSelector] Found old blob format, erasing...");
        nvs_erase_key(m_nvsHandle, nvsKeyToString(NvsKey::BATTERY_PROFILE).data());
        nvs_commit(m_nvsHandle);
    }

    // Load profile from NVS, if it fails load default profile
    if(loadProfileFromNVS() != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] WARNING: Failed to load profile from NVS, loading default profile.");
        setProfileType(BatteryConfig::BatteryType::LIION_3S);
    }
}

BatteryProfileSelector::Result BatteryProfileSelector::setProfileType(BatteryConfig::BatteryType type)
{
    const BatteryProfile& profile = BatteryConfig::getDefaultBatteryProfile(type);
    Result validationResult = validateProfile(profile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid profile parameters!");
        return validationResult;
    }

    m_currentType = type;
    m_currentProfile = profile;

    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::setMaxVoltage(int maxVoltage_mV)
{
    BatteryProfile modifiedProfile = m_currentProfile;
    modifiedProfile.maxVoltage_mV = maxVoltage_mV;

    Result validationResult = validateProfile(modifiedProfile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid max voltage parameter!");
        return validationResult;
    }

    m_currentProfile.maxVoltage_mV = maxVoltage_mV;
    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::setRechargeVoltage(int rechargeVoltage_mV)
{
    BatteryProfile modifiedProfile = m_currentProfile;
    modifiedProfile.rechargeVoltage_mV = rechargeVoltage_mV;

    Result validationResult = validateProfile(modifiedProfile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid recharge voltage parameter!");
        return validationResult;
    }

    m_currentProfile.rechargeVoltage_mV = rechargeVoltage_mV;
    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::setPrechargeVoltage(int prechargeVoltage_mV)
{
    BatteryProfile modifiedProfile = m_currentProfile;
    modifiedProfile.prechargeVoltage_mV = prechargeVoltage_mV;

    Result validationResult = validateProfile(modifiedProfile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid precharge voltage parameter!");
        return validationResult;
    }

    m_currentProfile.prechargeVoltage_mV = prechargeVoltage_mV;
    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::setLoadDisconnectVoltage(int loadDisconnectVoltage_mV)
{
    BatteryProfile modifiedProfile = m_currentProfile;
    modifiedProfile.loadDisconnectVoltage_mV = loadDisconnectVoltage_mV;

    Result validationResult = validateProfile(modifiedProfile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid load disconnect voltage parameter!");
        return validationResult;
    }

    m_currentProfile.loadDisconnectVoltage_mV = loadDisconnectVoltage_mV;
    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::setMaxChargingCurrent(int maxChargingCurrent_mA)
{
    BatteryProfile modifiedProfile = m_currentProfile;
    modifiedProfile.maxChargingCurrent_mA = maxChargingCurrent_mA;

    Result validationResult = validateProfile(modifiedProfile);
    if(validationResult != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Invalid max charging current parameter!");
        return validationResult;
    }

    m_currentProfile.maxChargingCurrent_mA = maxChargingCurrent_mA;
    return saveProfileToNVS();
}

BatteryProfileSelector::Result BatteryProfileSelector::validateProfile(const BatteryProfile& profile)
{
    const BatteryProfile& defaultProfile = BatteryConfig::getDefaultBatteryProfile(m_currentType);

    if(profile.maxVoltage_mV <= 0 || profile.rechargeVoltage_mV <= 0 || 
        profile.prechargeVoltage_mV <= 0 || profile.loadDisconnectVoltage_mV <= 0)
        return Result::VALIDATION_ERROR;

    // Allow some headroom above default max voltage, but prevent unrealistic values
    if(profile.maxVoltage_mV > defaultProfile.maxVoltage_mV * VOLTAGE_OVERRIDE_UPPER_MARGIN || 
        profile.maxVoltage_mV < defaultProfile.maxVoltage_mV * VOLTAGE_OVERRIDE_LOWER_MARGIN)
        return Result::VALIDATION_ERROR;

    if(profile.rechargeVoltage_mV >= profile.maxVoltage_mV)
        return Result::VALIDATION_ERROR;

    if(profile.prechargeVoltage_mV >= profile.rechargeVoltage_mV)
        return Result::VALIDATION_ERROR;

    if(profile.loadDisconnectVoltage_mV >= profile.prechargeVoltage_mV)
        return Result::VALIDATION_ERROR;

    if (profile.maxChargingCurrent_mA <= 0 || profile.maxChargingCurrent_mA > MAX_CHARGING_CURRENT)
        return Result::VALIDATION_ERROR;

    return Result::SUCCESS;
}

BatteryProfileSelector::Result BatteryProfileSelector::loadProfileFromNVS()
{
    // Step 1: Load battery type
    uint8_t batteryType{};
    esp_err_t err = nvs_get_u8(m_nvsHandle, nvsKeyToString(NvsKey::BATTERY_TYPE).data(), &batteryType);
    if (err != ESP_OK)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Failed to load battery type from NVS!");
        return Result::NVS_LOAD_ERROR;
    }

    m_currentType = static_cast<BatteryConfig::BatteryType>(batteryType);

    // Step 2: Load profile fields, if they exist, otherwise use defaults
    const BatteryProfile& defaultProfile = BatteryConfig::getDefaultBatteryProfile(m_currentType);
    m_currentProfile = defaultProfile; // Start with default profile

    // Step 3: Load and apply overrides
    int32_t overrideValue{};

    if(nvs_get_i32(m_nvsHandle, nvsKeyToString(NvsKey::MAX_VOLTAGE).data(), &overrideValue) == ESP_OK)
        m_currentProfile.maxVoltage_mV = overrideValue;

    if(nvs_get_i32(m_nvsHandle, nvsKeyToString(NvsKey::RECHARGE_VOLTAGE).data(), &overrideValue) == ESP_OK)
        m_currentProfile.rechargeVoltage_mV = overrideValue;

    if(nvs_get_i32(m_nvsHandle, nvsKeyToString(NvsKey::PRECHARGE_VOLTAGE).data(), &overrideValue) == ESP_OK)
        m_currentProfile.prechargeVoltage_mV = overrideValue;

    if(nvs_get_i32(m_nvsHandle, nvsKeyToString(NvsKey::LOAD_DISCONNECT_VOLTAGE).data(), &overrideValue) == ESP_OK)
        m_currentProfile.loadDisconnectVoltage_mV = overrideValue;

    if(nvs_get_i32(m_nvsHandle, nvsKeyToString(NvsKey::MAX_CHARGING_CURRENT).data(), &overrideValue) == ESP_OK)
        m_currentProfile.maxChargingCurrent_mA = overrideValue;

    nvs_commit(m_nvsHandle);

    // Validate the loaded profile to ensure consistency
    if(validateProfile(m_currentProfile) != Result::SUCCESS)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Corrupted profile data loaded from NVS!");
        // Fall back to default
        m_currentProfile = BatteryConfig::getDefaultBatteryProfile(m_currentType);
        return Result::NVS_LOAD_ERROR;
    }

    return Result::SUCCESS;
}

BatteryProfileSelector::Result BatteryProfileSelector::saveProfileToNVS()
{
    // Step 1: Save the profile TYPE
    esp_err_t err = nvs_set_u8(m_nvsHandle, nvsKeyToString(NvsKey::BATTERY_TYPE).data(), (uint8_t)m_currentType);
    if(err != ESP_OK)
    {
        Serial.println("[BatteryProfileSelector] ERROR: Failed to save battery type to NVS!");
        return Result::NVS_SAVE_ERROR;
    }

    // Step 2: Save ONLY fields that differ from default
    const BatteryProfile& defaultProfile = BatteryConfig::getDefaultBatteryProfile(m_currentType);
    if(m_currentProfile.maxVoltage_mV != defaultProfile.maxVoltage_mV)
    {
        esp_err_t err = nvs_set_i32(m_nvsHandle, nvsKeyToString(NvsKey::MAX_VOLTAGE).data(), m_currentProfile.maxVoltage_mV);
        if(err != ESP_OK)
        {
            Serial.println("[BatteryProfileSelector] ERROR: Failed to save max voltage to NVS!");
            return Result::NVS_SAVE_ERROR;
        }
    }
    if(m_currentProfile.rechargeVoltage_mV != defaultProfile.rechargeVoltage_mV)
    {
        esp_err_t err = nvs_set_i32(m_nvsHandle, nvsKeyToString(NvsKey::RECHARGE_VOLTAGE).data(), m_currentProfile.rechargeVoltage_mV);
        if(err != ESP_OK)
        {
            Serial.println("[BatteryProfileSelector] ERROR: Failed to save recharge voltage to NVS!");
            return Result::NVS_SAVE_ERROR;
        }
    }
    if(m_currentProfile.prechargeVoltage_mV != defaultProfile.prechargeVoltage_mV)
    {
        esp_err_t err = nvs_set_i32(m_nvsHandle, nvsKeyToString(NvsKey::PRECHARGE_VOLTAGE).data(), m_currentProfile.prechargeVoltage_mV);
        if(err != ESP_OK)
        {
            Serial.println("[BatteryProfileSelector] ERROR: Failed to save precharge voltage to NVS!");
            return Result::NVS_SAVE_ERROR;
        }
    }
    if(m_currentProfile.loadDisconnectVoltage_mV != defaultProfile.loadDisconnectVoltage_mV)
    {
        esp_err_t err = nvs_set_i32(m_nvsHandle, nvsKeyToString(NvsKey::LOAD_DISCONNECT_VOLTAGE).data(), m_currentProfile.loadDisconnectVoltage_mV);
        if(err != ESP_OK)
        {
            Serial.println("[BatteryProfileSelector] ERROR: Failed to save load disconnect voltage to NVS!");
            return Result::NVS_SAVE_ERROR;
        }
    }
    if(m_currentProfile.maxChargingCurrent_mA != defaultProfile.maxChargingCurrent_mA)
    {
        esp_err_t err = nvs_set_i32(m_nvsHandle, nvsKeyToString(NvsKey::MAX_CHARGING_CURRENT).data(), m_currentProfile.maxChargingCurrent_mA);
        if(err != ESP_OK)
        {
            Serial.println("[BatteryProfileSelector] ERROR: Failed to save max charging current to NVS!");
            return Result::NVS_SAVE_ERROR;
        }
    }

    nvs_commit(m_nvsHandle);

    return Result::SUCCESS;
}

void BatteryProfileSelector::printCurrentProfile() const
{
    Serial.println("Current Battery Profile:");
    Serial.printf("Type: %d\n", static_cast<int>(m_currentType));
    Serial.printf("Max Voltage (mV): %d\n", m_currentProfile.maxVoltage_mV);
    Serial.printf("Recharge Voltage (mV): %d\n", m_currentProfile.rechargeVoltage_mV);
    Serial.printf("Precharge Voltage (mV): %d\n", m_currentProfile.prechargeVoltage_mV);
    Serial.printf("Load Disconnect Voltage (mV): %d\n", m_currentProfile.loadDisconnectVoltage_mV);
    Serial.printf("Max Charging Current (mA): %d\n", m_currentProfile.maxChargingCurrent_mA);
}

const std::string_view BatteryProfileSelector::nvsKeyToString(NvsKey key) const
{
    switch(key)
    {
        case NvsKey::BATTERY_TYPE:
            return "battery_type";
        case NvsKey::BATTERY_PROFILE:
            return "battery_profile";
        case NvsKey::MAX_VOLTAGE:
            return "max_voltage";
        case NvsKey::RECHARGE_VOLTAGE:
            return "recharge_voltage";
        case NvsKey::PRECHARGE_VOLTAGE:
            return "precharge_voltage";
        case NvsKey::LOAD_DISCONNECT_VOLTAGE:
            return "load_disconnect_voltage";
        case NvsKey::MAX_CHARGING_CURRENT:
            return "max_charging_current";
        default:
            Serial.println("[BatteryProfileSelector] ERROR: Unsupported NVS key!");
            return "";
    }
}