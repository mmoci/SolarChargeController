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
        m_actuator->setBatteryProfile(profile);

        m_profileSelector->registerProfileObserver([this](const BatteryProfile& profile) {
            m_batteryManager.updateBatteryProfile(profile);
            m_actuator->setBatteryProfile(profile);
        });
    }
    else
    {
        // No profile selector provided (e.g. unit tests with a fixed profile) — seed
        // BatteryManager with a safe default so it is always in a valid state.
        m_batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
        m_actuator->setBatteryProfile(BatteryConfig::LI_ION_3S_DEFAULT);
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
        updatePvAvailability(measurementSnapshot.pv, measurementSnapshot.battery);

    if(measurementSnapshot.batteryValid)
        m_batteryManager.update(measurementSnapshot.battery, m_isPvAvailable);

    handleChargingStateChange(m_batteryManager.isChargingAllowed());
    handleVoltageLimitStateChange(m_batteryManager.isVoltageLimitActive());

    if (!m_batteryManager.isChargingAllowed())
        return;

    const int desiredSetpoint = computeDesiredSetpoint(measurementSnapshot);
    const int limitedSetpoint = applyLimitConstraints(desiredSetpoint, measurementSnapshot);

    m_mpptControl = limitedSetpoint;
    m_actuator->applyControl(std::max(limitedSetpoint, 1));
}

bool ChargeController::updatePvAvailability(Measurements pvMeasurements, Measurements batteryMeasurements)
{
    long pvPower_mW = static_cast<long>(pvMeasurements.voltage_mV) * pvMeasurements.current_mA / 1000;
    const bool pvAvailable {(pvPower_mW > ChargeControllerConfig::PV_POWER_THRESHOLD) || 
                            (pvMeasurements.voltage_mV > batteryMeasurements.voltage_mV + ChargeControllerConfig::PV_INPUT_HEADROOM_MV)};
    bool availabilityChanged{false};

    if (m_isPvAvailable != pvAvailable)
    {
        ESP_LOGI(TAG, "PV input %s Vbatt (Vin=%dmV Vbatt=%dmV)", pvAvailable ? "above" : "below", pvMeasurements.voltage_mV, batteryMeasurements.voltage_mV);
        availabilityChanged = true;
    }
        
    m_isPvAvailable = pvAvailable;

    return availabilityChanged;
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

    return constrain(mpptControl - mpptControlCorrection, 0, 100);
}

int ChargeController::softRampControl(int targetControl, int stepSize)
{
    if(targetControl > m_mpptControl)
            return std::min(targetControl, m_mpptControl + stepSize);
    else if(targetControl < m_mpptControl)
            return std::max(targetControl, m_mpptControl - stepSize);
    return targetControl;
}

void ChargeController::handleChargingStateChange(bool isChargingAllowed)
{
    if (!m_wasChargingAllowed && isChargingAllowed)
    {
        m_mpptStrategy->init();
        m_actuator->enableOutput(true); // Enable output on every charge-start transition (startup, recharge after full battery, etc.)
        ESP_LOGI(TAG, "Charging started — MPPT reset to initial state");
    }
    else if (m_wasChargingAllowed && !isChargingAllowed)
    {
        m_actuator->enableOutput(false, true); // Urgent: preempt any pending writes to immediately cut output
        m_actuator->applyControl(0);           // Reset setpoint to 0 so MPPT restarts clean from 0% on next charge cycle
        ESP_LOGI(TAG, "Charging stopped");
    }
    m_wasChargingAllowed = isChargingAllowed;
}

void ChargeController::handleVoltageLimitStateChange(bool isVoltageLimitActive)
{
    const bool cvReleased = m_wasVoltageLimitActive && !isVoltageLimitActive;
    if (cvReleased && m_wasChargingAllowed) // m_wasChargingAllowed already updated by handleChargingStateChange
    {
        m_mpptStrategy->init();
        m_voltageIntegralError = 0;
        ESP_LOGI(TAG, "CV limit released — MPPT reset to initial state");
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
    snapshot.pvValid      = m_pvMeasurements->isMeasurementValid();
    snapshot.batteryValid = m_batteryMeasurements->isMeasurementValid();
    // isMeasurementUpdated() has a side effect (records last age) — call exactly
    // once per cycle, and only when PV is valid (short-circuit prevents spurious call).
    snapshot.updated = snapshot.pvValid && m_pvMeasurements->isMeasurementUpdated();
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
        return std::max(0, m_mpptControl - 10);
    }

    if (snapshot.updated)
    {
        const bool settled = m_actuator->areMeasurementsSettled();
        if (settled && m_wasChargingAllowed && !m_wasVoltageLimitActive)
        {
            m_mpptStrategy->update(snapshot.pv);
            m_mpptControl = softRampControl(m_mpptStrategy->getMpptControl(), m_mpptStrategy->getMaxSoftRampStep());
        }
    }

    return m_mpptControl;
}

int ChargeController::applyLimitConstraints(int desiredSetpoint, MeasurementSnapshot snapshot)
{
    int limitedSetpoint = desiredSetpoint;

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
