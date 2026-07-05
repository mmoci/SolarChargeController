#include "ChargeController.h"
#include "DPSxDcConverter.h"
#include "PwmDcConverter.h"
#include "Logger.h"

static constexpr char TAG[] = "ChargeController";

void ChargeController::init()
{
    if(m_profileSelector)
    {
        m_profileSelector->init();
        const BatteryProfile& profile = m_profileSelector->getCurrentProfile();
        m_batteryManager.init(profile);
        m_actuator->setOverVoltageProtection(profile.maxVoltage_mV);

        m_profileSelector->registerProfileObserver([this](const BatteryProfile& profile) {
            m_batteryManager.updateBatteryProfile(profile);
            m_actuator->setOverVoltageProtection(profile.maxVoltage_mV);
        });
    }
    else
    {
        // No profile selector provided (e.g. unit tests with a fixed profile) — seed
        // BatteryManager with a safe default so it is always in a valid state.
        m_batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
        m_actuator->setOverVoltageProtection(BatteryConfig::LI_ION_3S_DEFAULT.maxVoltage_mV);
    }

    // Actuator and corresponding MPPT strategy are already initialized by Initializer with a default profile, 
    // so we can call init() on the strategy now that the profile is set.
    if(m_mpptStrategy)
        m_mpptStrategy->init();
}

void ChargeController::update()
{
    const auto measurementSnapshot{sampleMeasurements()};

    if(measurementSnapshot.pvValid)
        updatePvPresence(measurementSnapshot.pv, measurementSnapshot.battery);

    if(measurementSnapshot.batteryValid)
        m_batteryManager.update(measurementSnapshot.battery, m_isPvPresent);

    handleBatteryChargingStates(m_batteryManager.isChargingAllowed(), m_batteryManager.isVoltageLimitActive());

    // If charging is not allowed, skip MPPT updates and hold the last control value.
    if (!m_batteryManager.isChargingAllowed())
        return;

    const int desiredSetpoint = computeDesiredSetpoint(measurementSnapshot);
    const int limitedSetpoint = applyLimitConstraints(desiredSetpoint, measurementSnapshot);

    m_mpptControl = limitedSetpoint;
    m_actuator->applyControl(std::max(limitedSetpoint, 1));
    if(m_mpptStrategy)
        m_mpptStrategy->syncControl(m_mpptControl);
}

void ChargeController::updatePvPresence(Measurements pvMeasurements, Measurements batteryMeasurements)
{
    long pvPower_mW = static_cast<long>(pvMeasurements.voltage_mV) * pvMeasurements.current_mA / 1000;
    const bool isPvPresent {(pvMeasurements.voltage_mV > std::max(batteryMeasurements.voltage_mV, ChargeControllerConfig::PANEL_PRESENT_MIN_OUTPUT_VOLTAGE_mV)
                            + ChargeControllerConfig::PANEL_PRESENT_VIN_HEADROOM_mV) || (pvPower_mW > ChargeControllerConfig::PANEL_PRESENT_MIN_OUTPUT_POWER_mW)};

    if (isPvPresent != m_isPvPresent)
        ESP_LOGI(TAG, "PV %s (Vin=%dmV Vbatt=%dmV Power=%ldmW)", isPvPresent ? "present" : "not present",
                 pvMeasurements.voltage_mV, batteryMeasurements.voltage_mV, pvPower_mW);

    m_isPvPresent = isPvPresent;
}

int ChargeController::clampLimitPI(int measured, int limit, int mpptControl, long& integralError)
{
    if (measured <= 0) 
        return mpptControl;

    int error = measured - limit;
    
    if(error <= 0) 
    {
        // Measured is within limit — MPPT is in control, this function does nothing.
        // Reset integral so stale windup from a past violation does not amplify the
        // first correction of the next independent limiting event.
        integralError = 0;
        return mpptControl;
    }

    // Used PI (Proportional & Integral) method for compensation as gives better results than linear approach 
    integralError += error;
    integralError = constrain(integralError, 0L, ChargeControllerConfig::MAX_INTEGRAL_ERROR);
    int mpptControlCorrection = static_cast<int>(std::roundl(ChargeControllerConfig::Kp * error + ChargeControllerConfig::Ki * integralError));

    return constrain(mpptControl - mpptControlCorrection, 0, MpptStrategyIf::MAX_CONTROL_VALUE);
}

int ChargeController::softRampControl(int currentMpptControl, int stepSize)
{
    if(currentMpptControl > m_mpptControl)
            return std::min(currentMpptControl, m_mpptControl + stepSize);
    else if(currentMpptControl < m_mpptControl)
            return currentMpptControl; // Allow immediate reductions in control to respond to rapidly changing conditions (e.g. cloud passing over panel)
    return currentMpptControl;
}

void ChargeController::handleBatteryChargingStates(bool isChargingAllowed, bool isVoltageLimitActive)
{
    if (!m_wasChargingAllowed && isChargingAllowed)
    {
        ESP_LOGI(TAG, "Battery charging started");
        resetMpptStrategy();
        m_actuator->enableOutput(true); // Enable output on every charge-start transition (startup, recharge after full battery, etc.)
    }
    else if (m_wasChargingAllowed && !isChargingAllowed)
    {
        ESP_LOGI(TAG, "Battery charging stopped");
        m_actuator->enableOutput(false, true); // Urgent: preempt any pending writes to immediately cut output
        m_actuator->applyControl(0);           // Reset setpoint to 0 so MPPT restarts clean from 0% on next charge cycle
    }
    m_wasChargingAllowed = isChargingAllowed;

    const bool isCvModeReleased = m_wasVoltageLimitActive && !isVoltageLimitActive;
    if (isCvModeReleased && m_wasChargingAllowed) 
    {
        ESP_LOGI(TAG, "Battery CV mode limit released");
        resetMpptStrategy();
        m_voltageIntegralError = 0;
    }
    m_wasVoltageLimitActive = isVoltageLimitActive;
}

ChargeController::MeasurementSnapshot ChargeController::sampleMeasurements()
{
    MeasurementSnapshot snapshot{};
    snapshot.pv.voltage_mV = m_pvMeasurements->getVoltage_mV();
    snapshot.pv.current_mA = m_pvMeasurements->getCurrent_mA();

    auto pvOpenCircuitVoltageOpt = m_pvMeasurements->getOpenCircuitVoltage_mV();
    if(pvOpenCircuitVoltageOpt.has_value())
    {
        snapshot.pvOpenCircuitVoltage_mV = pvOpenCircuitVoltageOpt.value();
        m_mpptStrategy->setOpenCircuitVoltage(snapshot.pvOpenCircuitVoltage_mV);
    }
    
    snapshot.battery.voltage_mV = m_batteryMeasurements->getVoltage_mV();
    snapshot.battery.current_mA = m_batteryMeasurements->getCurrent_mA();
    snapshot.pvValid            = m_pvMeasurements->isMeasurementValid();
    snapshot.batteryValid       = m_batteryMeasurements->isMeasurementValid();
    snapshot.updated            = snapshot.pvValid && m_pvMeasurements->isMeasurementUpdated();
    snapshot.measurementSettled = snapshot.pvValid && m_pvMeasurements->isMeasurementSettled();
    return snapshot;
}

int ChargeController::computeDesiredSetpoint(MeasurementSnapshot snapshot)
{
    if (!snapshot.pvValid)
    {
        static unsigned long lastPvStaleLog{0};
        const unsigned long now{millis()};
        if (now - lastPvStaleLog >= ChargeControllerConfig::STALE_LOG_INTERVAL)
        {
            ESP_LOGW(TAG, "PV measurements stale — holding control at %d%%", m_mpptControl);
            lastPvStaleLog = now;
        }
        return m_mpptControl;
    }

    if (!snapshot.batteryValid)
    {
        static unsigned long lastBattStaleLog{0};
        const unsigned long now{millis()};
        if (now - lastBattStaleLog >= ChargeControllerConfig::STALE_LOG_INTERVAL)
        {
            ESP_LOGW(TAG, "Battery measurements stale — reducing control");
            lastBattStaleLog = now;
        }
        return std::max(0, m_mpptControl - MpptStrategyIf::MAX_CONTROL_VALUE / 10); // Reduce control by 10% if battery measurements are stale, as a precaution against overcharging due to undetected high voltage
    }

    if (snapshot.updated)
    {
        const bool isOutputEnabled {m_actuator->isOutputEnabled()};

        if (isOutputEnabled && !m_wasOutputEnabled && m_wasChargingAllowed)
        {
            ESP_LOGI(TAG, "Output re-enabled while charging allowed — resetting MPPT to prevent stale setpoint application");
            resetMpptStrategy();
        }
        m_wasOutputEnabled = isOutputEnabled;

        if (isOutputEnabled && m_wasChargingAllowed && !m_wasVoltageLimitActive && snapshot.measurementSettled)
        {
            m_mpptStrategy->update(snapshot.pv);
            m_mpptControl = softRampControl(m_mpptStrategy->getMpptControl(), m_mpptStrategy->getMaxSoftRampStep());
        }
        else if (!isOutputEnabled && m_wasOutputEnabled)
        {
            ESP_LOGD(TAG, "Output disabled — holding control at %.2f%%", m_mpptControl / static_cast<float>(MpptStrategyIf::MAX_CONTROL_VALUE) * 100);
        }   
        else if (!snapshot.measurementSettled)
        {
            ESP_LOGD(TAG, "Measurements not settled — holding control at %.2f%%", m_mpptControl / static_cast<float>(MpptStrategyIf::MAX_CONTROL_VALUE) * 100);
        }  
    }

    return m_mpptControl;
}

int ChargeController::applyLimitConstraints(int desiredSetpoint, MeasurementSnapshot snapshot)
{
    int limitedSetpoint {desiredSetpoint};

    if(m_batteryManager.isCurrentLimitActive() && snapshot.updated)
    {
        if(auto currentLimit = m_batteryManager.getMaxChargingCurrentLimit())
        {
            limitedSetpoint = clampLimitPI(snapshot.battery.current_mA, *currentLimit, limitedSetpoint, m_currentIntegralError);
        }
    }

    if(m_batteryManager.isVoltageLimitActive() && snapshot.updated)
    {
        if(auto voltageLimit = m_batteryManager.getMaxVoltageLimit())
        {
            limitedSetpoint = clampLimitPI(snapshot.battery.voltage_mV, *voltageLimit, limitedSetpoint, m_voltageIntegralError);
        }
    }

    return limitedSetpoint;
}

void ChargeController::resetMpptStrategy()
{
    m_mpptStrategy->init();
    auto ocvOpt = m_pvMeasurements->getOpenCircuitVoltage_mV();
    if (ocvOpt.has_value())
        m_mpptStrategy->setOpenCircuitVoltage(ocvOpt.value());
    ESP_LOGI(TAG, "MPPT reset, new Voc=%dmV", ocvOpt.value_or(-1));
}
