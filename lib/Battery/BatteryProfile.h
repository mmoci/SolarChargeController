#pragma once

struct BatteryProfile
{
    enum class BatteryChemistry
    {
        LiIon,
        LiFePO4
    };

    BatteryChemistry chemistry{};
        
    int maxVoltage_mV{};         // When this limit is reached change Mode from CC to CV
    int rechargeVoltage_mV{};    // When this limit is reached start recharging battery (hysteresis when Mode is Done)
    int prechargeVoltage_mV{};   // When battery voltage is below this limit, set Mode to Precharge
    int minSafeVoltage_mV{};     // When voltage is below this limit, set mode state to Falut
    int loadDisconnectVoltage_mV{};  // When voltage is below this limit, cutoff load (deep-discharge protection)

    int maxChargingCurrent_mA{}; // Max allowed charging current for the battery (checked in CC Mode)
    int cutoffCurrent_mA{};      // When charging current is below this limit, battery is full, cutoff charging process, set mode to Done
    int prechargeCurrent_mA{};   // Max allowed charging current when Mode is set to Precharge
    int idleCurrent_mA{};        // Max allowed current in Idle mode
};