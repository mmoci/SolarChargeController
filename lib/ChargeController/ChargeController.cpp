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
    int mpptControl{};
    bool newMeasurement{m_pvMeasurements->isMeasurementUpdated()}; // Tracks whether new hardware reading arrived this cycle. Used to gate MPPT updates and soft ramping so they only run once per new measurement, preventing them from running at loop speed (~6ms) and causing excessive ramping when measurements are stale (e.g. DPS disconnected, Modbus comms issue).

    Measurements pvMeasurements{m_pvMeasurements->getVoltage_mV(), m_pvMeasurements->getCurrent_mA()};
    Measurements batteryMeasurements{m_batteryMeasurements->getVoltage_mV(), m_batteryMeasurements->getCurrent_mA()};

    if(!m_pvMeasurements->isMeasurementValid())
    {
        static unsigned long lastPvStaleLog{0};
        const unsigned long now{millis()};
        if (now - lastPvStaleLog >= ChargeControllerConfig::STALE_LOG_INTERVAL)
        {
            ESP_LOGW(TAG, "PV measurements stale — holding control at %d%%", m_mpptControl);
            lastPvStaleLog = now;
        }
        mpptControl = m_mpptControl;  // Maintain last known control to prevent sudden jumps when measurements are stale, but don't reset to 0 to allow soft recovery when PV measurements become valid again
    }
    else if(!m_batteryMeasurements->isMeasurementValid())
    {
        static unsigned long lastBattStaleLog{0};
        const unsigned long now{millis()};
        if (now - lastBattStaleLog >= ChargeControllerConfig::STALE_LOG_INTERVAL)
        {
            ESP_LOGW(TAG, "Battery measurements stale — reducing control");
            lastBattStaleLog = now;
        }
        mpptControl = std::max(0, m_mpptControl - 10);  // Reduce charging current to prevent potential overcharging when battery measurements are unavailable
    }
    else
    {
        // Convert to mW to avoid overflow and match typical PV power units
        auto pvPower_mW = static_cast<long>(pvMeasurements.voltage_mV) * pvMeasurements.current_mA / 1000;
        updatePvAvailability(pvPower_mW, pvMeasurements.voltage_mV, batteryMeasurements.voltage_mV);

        if (newMeasurement && m_wasChargingAllowed && !m_wasVoltageLimitActive && m_actuator->areMeasurementsSettled())
        {
            // Only perturb MPPT while actively charging. When not charging (Idle/Done/Fault)
            // there is no feedback signal (Iout=0, ΔV≈0) and the algorithm would wander
            // blindly. On the transition to charging, MPPT is reset below so it always
            // starts fresh from a known state.
            // areMeasurementsSettled() skips P&O on the first read after a write: the DPS
            // needs ~1 read cycle (~450ms) before IOUT reflects the new I_SET. Computing
            // gradients against the stale reading causes false direction flips.
            // !m_wasVoltageLimitActive: when the CV voltage limit is active, MPPT yields to
            // the PI controller. Running P&O on data from an unloaded panel (Vin=Voc, Iout≈0)
            // would corrupt the gradient and cause a full-speed ramp-up once CV releases.
            m_mpptController.update(pvMeasurements);
        }

        // Soft ramp is gated on new settled measurements. Without this gate the ramp
        // advances at Arduino loop rate (~6ms): between two Modbus reads (~450ms) it
        // can advance ~75 steps, climbing from 0% back to 100% in a single read period
        // and defeating the rate-limit for DPS-mode operation.
        // When voltage-limited (CV active), hold the last control value; clampLimitPI
        // below will determine the actual output through the PI controller.
        if (newMeasurement && !m_wasVoltageLimitActive)
            mpptControl = softRampControl(m_mpptController.getRequestedOutput(), m_actuator->getMaxSoftStep());
        else
            mpptControl = m_mpptControl;

        if (newMeasurement)
            handlePvCollapse(pvMeasurements.voltage_mV, batteryMeasurements.voltage_mV);
    }

    // Only update battery state machine when measurements are valid.
    // Passing Vbatt=0 (no DPS response yet) would immediately trip the
    // minSafeVoltage fault — a terminal state with no recovery.
    if(m_batteryMeasurements->isMeasurementValid())
        m_batteryManager.update(batteryMeasurements, m_isPvAvailable);

    // Detect the not-charging → charging transition and reset MPPT to a clean
    // state. This ensures MPPT always starts from control=0 / direction=Up
    // rather than from a stale value accumulated while idle or from a previous
    // charge cycle that wandered to an arbitrary operating point.
    const bool chargingAllowed{m_batteryManager.isChargingAllowed()};
    handleChargingStateChange(chargingAllowed);

    // Detect the CV → CC transition (voltage limit released). While CV was active MPPT
    // was frozen; reset it so it restarts clean rather than re-using gradient data from
    // when the panel was unloaded (Vin≈Voc, Iout≈0). Also reset mpptControl and the
    // voltage PI integral to eliminate windup accumulated during the CV phase.
    // Guard: only reset on CV→CC (chargingAllowed still true). Do NOT fire on CV→Done
    // or CV→Fault, where charging is disabled — in those cases the MPPT stays at 0
    // and the output-disable path below handles the actuator.
    const bool voltageLimitActive{m_batteryManager.isVoltageLimitActive()};
    if (m_wasVoltageLimitActive && !voltageLimitActive && chargingAllowed)
    {
        m_mpptController.init();
        mpptControl = 0;
        m_voltageIntegralError = 0;
        ESP_LOGI(TAG, "CV limit released — MPPT reset to initial state");
    }
    m_wasVoltageLimitActive = voltageLimitActive;

    if(!chargingAllowed)
        return; // Charging not active — nothing more to do this cycle

    // Gate PI controllers on newMeasurement: the PI must advance at the hardware
    // feedback rate (one Modbus read per ~900 ms), not at Arduino loop rate (~3 ms).
    // Without this gate, Kp=1.0 with 40 mV error drives mpptControl to 0 in ~9 ms
    // (3 iterations), causing premature CV→Done on reboot when Vbatt > maxVoltage.
    if (newMeasurement)
    {
        if(m_batteryManager.isCurrentLimitActive())
        {
            if(auto currentLimit = m_batteryManager.getMaxChargingCurrentLimit())
            {
                mpptControl = clampLimitPI(batteryMeasurements.current_mA, *currentLimit, mpptControl, m_currentIntegralError);
            }
        }

        if(m_batteryManager.isVoltageLimitActive())
        {
            if(auto voltageLimit = m_batteryManager.getMaxVoltageLimit())
            {
                mpptControl = clampLimitPI(batteryMeasurements.voltage_mV, *voltageLimit, mpptControl, m_voltageIntegralError);
            }
        }
    }

    if(m_batteryManager.isLoadDisconnectVoltageLimitActive(batteryMeasurements.voltage_mV))
    {
        
        // TODO: If battery voltage is below load disconnect voltage limit, cutoff load to prevent deep-discharge. 
        // This is a safety feature to protect the battery from damage due to over-discharge, 
        // which can occur if the battery is drained below a certain voltage threshold.

        // TODO: Consider implementing hysteresis or a delay before re-enabling charging after load disconnect to prevent rapid on/off 
        // cycling if battery voltage hovers around the threshold.
    }

    m_mpptControl = mpptControl;

    // Never let the MPPT path call applyControl(0) — 0 disables the output.
    // The hardware voltage floor means MPPT's internal percentage can drift below
    // the floor without any observable effect (ΔV≈0), eventually reaching 0%.
    // The intentional disable path is exclusively via !isChargingAllowed() above.
    m_actuator->applyControl(std::max(mpptControl, 1));
}

bool ChargeController::updatePvAvailability(long pvPower_mW, int pvVoltage_mV, int battVoltage_mV)
{
    const bool pvAvailable {(pvPower_mW > ChargeControllerConfig::PV_POWER_THRESHOLD) || (pvVoltage_mV > battVoltage_mV + ChargeControllerConfig::PV_INPUT_HEADROOM_MV)};
    bool availabilityChanged{false};

    if (m_isPvAvailable != pvAvailable)
    {
        ESP_LOGI(TAG, "PV input %s Vbatt (Vin=%dmV Vbatt=%dmV)", pvAvailable ? "above" : "below", pvVoltage_mV, battVoltage_mV);
        availabilityChanged = true;
    }
        
    m_isPvAvailable = pvAvailable;
    m_pvPower_mW  = pvPower_mW;

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

void ChargeController::handleChargingStateChange(bool chargingAllowed)
{
    if (!m_wasChargingAllowed && chargingAllowed)
    {
        m_mpptController.init();
        m_actuator->enableOutput(true); // Enable output on every charge-start transition (startup, recharge after full battery, etc.)
        ESP_LOGI(TAG, "Charging started — MPPT reset to initial state");
    }
    else if (m_wasChargingAllowed && !chargingAllowed)
    {
        m_actuator->enableOutput(false, true); // Urgent: preempt any pending writes to immediately cut output
        m_actuator->applyControl(0);           // Reset setpoint to 0 so MPPT restarts clean from 0% on next charge cycle
        ESP_LOGI(TAG, "Charging stopped");
    }
    m_wasChargingAllowed = chargingAllowed;
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
