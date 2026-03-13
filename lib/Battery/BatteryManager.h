#pragma once

#include <Arduino.h>
#include <optional>
#include "BatteryProfile.h"
#include "Utility.h"

class BatteryManager
{
    public:
    /**
     * @brief Contains different charging states:
     *        Idle	    Charging not allowed
     *        Precharge	Charging allowed with very low current
     *        CC        Charging allowed with max current
     *        CV	    Charging allowed with voltage clamp
     *        Done	    Charging not allowed (battery full)
     *        Fault	    Charging not allowed (safety)
     */
    enum class Mode
    {
        Idle,
        Precharge,
        CC,
        CV,
        Done,
        Fault
    };

    /**
     * @brief Executes during Arduino setup(), saves provided BatteryProfile profile
     * 
     * @param profile BatteryProfile that will be used to read battery limits
     */
    void init(const BatteryProfile& profile);
    
    /**
     * @brief Executes continously in the loop(). 
     *        Uses received inputs (battery voltage and current) from sensor and compares to voltage
     *        and current limits. 
     *        Sets battery mode based on compared result and received charging available state.
     * 
     * @param batteryMeasurements Measured batter voltage and current
     * @param chargingAvailable True if charging is available, false otherwise
     */
    void update(const Measurements& batteryMeasurements, bool chargingAvailable);

    /**
     * @brief When CV mode is active returns true as voltage limit is active.
     * 
     * @return true When mode is set to CV, false otherwise
     */
    bool isVoltageLimitActive() const;

    /**
     * @brief When CC mode is active returns true as current limit is active.
     * 
     * @return true When mode is set to CC, false otherwise
     */
    bool isCurrentLimitActive() const;

    /**
     * @brief Returns true when voltage is below load disconnect voltage limit, false otherwise.
     * 
     * @param batteryVoltage Measured battery voltage in mV
     * @return true When voltage is below load disconnect voltage limit, false otherwise
     */
    bool isLoadDisconnectVoltageLimitActive(int batteryVoltage) const;

    /**
     * @brief Return true if charging is allowed, false otherwise.
     * 
     * @return true if charging is allowed, false otherwise
     */
    bool isChargingAllowed() const;

    /**
     * @brief Get the Mode state.
     * 
     * @return Mode Current mode state
     */
    Mode getMode() const {return m_mode;}

    /**
     * @brief Get the Max Voltage Limit.
     * 
     * @return int Selected max voltage limit
     */
    std::optional<int> getMaxVoltageLimit() const 
    {
        if(m_mode == Mode::CV) return m_profile.maxVoltage_mV;
        return std::nullopt;
    }

    /**
     * @brief Get the Max Charging Current Limit.
     * 
     * @return int Selected max charging current limit
     */
    std::optional<int> getMaxChargingCurrentLimit() const 
    {
        if(m_mode == Mode::CC) return m_profile.maxChargingCurrent_mA;
        if(m_mode == Mode::Precharge) return m_profile.prechargeCurrent_mA;
        return std::nullopt;
    }

    private:
    BatteryProfile m_profile{};
    Mode m_mode{};
    bool m_chargingAvailable{false};
    Timer m_chargingDisabledTimer{};

    static constexpr long CHARGING_DISABLED_TMO{60000}; // 60 sec

    void handleIdleMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handlePrechargeMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handleCcMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handleCvMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handleDoneMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handleFaultMode(const Measurements& batteryMeasurements, bool chargingAvailable);
    void handleChargingDisabledTimer(bool chargingAvailable);
};