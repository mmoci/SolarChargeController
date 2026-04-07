#include "BatteryManager.h"

void BatteryManager::init(const BatteryProfile& profile)
{
    m_profile = profile;
    m_mode = Mode::Idle;
    m_chargingAvailable = false;
}

void BatteryManager::update(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    handleChargingDisabledTimer(chargingAvailable);

    switch(m_mode)
    {
        case Mode::Idle:
        handleIdleMode(batteryMeasurements, chargingAvailable);
        break;

        case Mode::Precharge:
        handlePrechargeMode(batteryMeasurements, chargingAvailable);
        break;

        case Mode::CC:
        handleCcMode(batteryMeasurements, chargingAvailable);
        break;

        case Mode::CV:
        handleCvMode(batteryMeasurements, chargingAvailable);
        break;

        case Mode::Done:
        handleDoneMode(batteryMeasurements, chargingAvailable);
        break;

        case Mode::Fault:
        handleFaultMode(batteryMeasurements, chargingAvailable);
        break;
    }    
}

void BatteryManager::updateBatteryProfile(const BatteryProfile& profile)
{
    m_profile = profile;
}

// Mode state handlers

void BatteryManager::handleIdleMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    auto& [batteryVoltage, batteryCurrent] = batteryMeasurements;

    if(batteryVoltage <= m_profile.minSafeVoltage_mV) 
    {
        m_mode = Mode::Fault;
    }
    else if(batteryVoltage <= m_profile.prechargeVoltage_mV && batteryVoltage > m_profile.minSafeVoltage_mV && chargingAvailable)
    {
        m_mode = Mode::Precharge;
    }
    else if(batteryVoltage > m_profile.prechargeVoltage_mV && chargingAvailable)
    {
        m_mode = Mode::CC;
    }
}

void BatteryManager::handlePrechargeMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    auto& [batteryVoltage, batteryCurrent] = batteryMeasurements;

    if(batteryVoltage <= m_profile.minSafeVoltage_mV)
    {   
        m_mode = Mode::Fault;
    }
    else if(!chargingAvailable && m_chargingDisabledTimer.getDuration() > CHARGING_DISABLED_TMO)
    {
        m_mode = Mode::Idle;
    }
    else if(batteryVoltage < m_profile.maxVoltage_mV && batteryVoltage > m_profile.prechargeVoltage_mV && chargingAvailable)
    {
        m_mode = Mode::CC;
    }
}

void BatteryManager::handleCcMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    auto& [batteryVoltage, batteryCurrent] = batteryMeasurements;

    if(batteryVoltage <= m_profile.minSafeVoltage_mV)
    {   
        m_mode = Mode::Fault;
    }
    else if(!chargingAvailable && m_chargingDisabledTimer.getDuration() > CHARGING_DISABLED_TMO)
    {
        m_mode = Mode::Idle;
    }
    else if(batteryVoltage >= m_profile.maxVoltage_mV && chargingAvailable)
    {
        m_mode = Mode::CV;
    }
}

void BatteryManager::handleCvMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    auto& [batteryVoltage, batteryCurrent] = batteryMeasurements;

    if(batteryVoltage <= m_profile.minSafeVoltage_mV || batteryVoltage > (m_profile.maxVoltage_mV * 105) / 100)
    {   
        m_mode = Mode::Fault;
    }
    else if(!chargingAvailable && m_chargingDisabledTimer.getDuration() > CHARGING_DISABLED_TMO)
    {
        m_mode = Mode::Idle;
    }
    else if(batteryVoltage >= m_profile.maxVoltage_mV && batteryCurrent <= m_profile.cutoffCurrent_mA)
    {
        m_mode = Mode::Done;
    }
}

void BatteryManager::handleDoneMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    auto& [batteryVoltage, batteryCurrent] = batteryMeasurements;
    
    if(batteryVoltage <= m_profile.minSafeVoltage_mV || batteryVoltage > (m_profile.maxVoltage_mV * 105) / 100)
    {   
        m_mode = Mode::Fault;
    }
    else if(!chargingAvailable && m_chargingDisabledTimer.getDuration() > CHARGING_DISABLED_TMO)
    {
        m_mode = Mode::Idle;
    }
    else if(batteryVoltage <= m_profile.rechargeVoltage_mV && chargingAvailable)
    {
        m_mode = Mode::CC;
    }
}

void BatteryManager::handleFaultMode(const Measurements& batteryMeasurements, bool chargingAvailable)
{
    // We will not start charging in faulty state even with small current as battery chemistry could lead to fire.
    Serial.println("[BatteryManager] Faulty state!");
}

void BatteryManager::handleChargingDisabledTimer(bool chargingAvailable)
{
    if(!chargingAvailable)
    {
        if(chargingAvailable != m_chargingAvailable)
            m_chargingDisabledTimer.trigger();
        else
            m_chargingDisabledTimer.update();
    }
    else
    {
        m_chargingDisabledTimer.reset();
    }

    m_chargingAvailable = chargingAvailable;
}

// Limits

bool BatteryManager::isVoltageLimitActive() const
{
    return m_mode == Mode::CV;
}

bool BatteryManager::isCurrentLimitActive() const
{
    return m_mode == Mode::CC || m_mode == Mode::Precharge;
}

bool BatteryManager::isLoadDisconnectVoltageLimitActive(int batteryVoltage) const
{
    return batteryVoltage < m_profile.loadDisconnectVoltage_mV;
}

bool BatteryManager::isChargingAllowed() const
{
    return m_mode == Mode::Precharge || m_mode == Mode::CC || m_mode == Mode::CV;
}
