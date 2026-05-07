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

    m_mpptController.init();
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
        m_mpptController.init();
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
        m_mpptController.init();
        m_voltageIntegralError = 0;
        ESP_LOGI(TAG, "CV limit released — MPPT reset to initial state");
    }
    m_wasVoltageLimitActive = isVoltageLimitActive;
}

void ChargeController::handlePvCollapse(int pvVoltage_mV, int battVoltage_mV)
{
    if(m_mpptController.isPvPowerCollapsing() && !m_collapsingRecoveryRequested)
    {
        ESP_LOGW(TAG, "PV collapse detected (Vin=%dmV Vbatt=%dmV) — disabling output", pvVoltage_mV, battVoltage_mV);
        m_actuator->enableOutput(false, true); // Urgent: preempts queue to immediately cut DPS output
        m_collapsingRecoveryRequested = true;
    }
    else if(m_collapsingRecoveryRequested && m_actuator->areMeasurementsSettled())
    {
        // DPS has processed the disable and settled. Apply the backed-off I_SET first,
        // then enable: FIFO queue ordering ensures I_SET reaches DPS before ON_OFF=1.
        m_actuator->applyControl(m_mpptController.getRequestedOutput());
        m_actuator->enableOutput(true); // Non-urgent: goes behind I_SET in the write queue
        m_collapsingRecoveryRequested = false;
        ESP_LOGI(TAG, "PV collapse recovery complete — output re-enabled at %d%%", m_mpptController.getRequestedOutput());
    }
}

ChargeController::MeasurementSnapshot ChargeController::sampleMeasurements()
{
    MeasurementSnapshot snapshot{};
    snapshot.pv.voltage_mV = m_pvMeasurements->getVoltage_mV();
    snapshot.pv.current_mA = m_pvMeasurements->getCurrent_mA();
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
        if (settled)
            handlePvCollapse(snapshot.pv.voltage_mV, snapshot.battery.voltage_mV);

        if (settled && m_wasChargingAllowed && !m_wasVoltageLimitActive)
        {
            m_mpptController.update(snapshot.pv);
            m_mpptControl = softRampControl(m_mpptController.getRequestedOutput(), m_actuator->getMaxSoftStep());
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
