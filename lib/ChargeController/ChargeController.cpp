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
        m_batteryManager.init(m_profileSelector->getCurrentProfile());

        m_profileSelector->registerProfileObserver([this](const BatteryProfile& profile) {
            m_batteryManager.updateBatteryProfile(profile);
        });
    }
    else
    {
        // No profile selector provided (e.g. unit tests with a fixed profile) — seed
        // BatteryManager with a safe default so it is always in a valid state.
        m_batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
    }

    m_mpptController.init();
}

void ChargeController::update()
{
    int mpptControl{};

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
        handlePvPowerUnavailableTimer(static_cast<long>(pvMeasurements.voltage_mV) * pvMeasurements.current_mA / 1000);

        // Only perturb the MPPT operating point when a genuinely new PV measurement
        // has arrived. With the DPS (Modbus ~100ms cycle) the control loop runs ~15x
        // per measurement update. Perturbing on stale data causes the P&O algorithm
        // to see ΔP≈0 and toggle direction randomly, degrading tracking accuracy.
        // lastTimeUpdated() returns ms-since-last-hardware-read; we perturb only when
        // that value is smaller than the previous recorded update time, i.e. a new
        // read has completed since our last perturbation.
        const unsigned long pvUpdateAge{m_pvMeasurements->lastTimeUpdated()};
        if (pvUpdateAge < m_lastPvUpdateAge || m_lastPvUpdateAge == 0)
        {
            m_mpptController.update(pvMeasurements);
        }
        m_lastPvUpdateAge = pvUpdateAge;

        mpptControl = m_mpptController.getRequestedOutput();

        // Implements a soft ramping mechanism for the MPPT control value. The soft ramping is applied on every update to ensure smooth transitions in control output.
        mpptControl = softRampControl(mpptControl, ChargeControllerConfig::MAX_CONTROL_SOFT_STEP);
    }

    // Only update battery state machine when measurements are valid.
    // Passing Vbatt=0 (no DPS response yet) would immediately trip the
    // minSafeVoltage fault — a terminal state with no recovery.
    if(m_batteryMeasurements->isMeasurementValid())
        m_batteryManager.update(batteryMeasurements, isChargingAvailable());

    if(!m_batteryManager.isChargingAllowed())
    {
        m_actuator->applyControl(0);
        return;
    }

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

    if(m_batteryManager.isLoadDisconnectVoltageLimitActive(batteryMeasurements.voltage_mV))
    {
        
        // TODO: If battery voltage is below load disconnect voltage limit, cutoff load to prevent deep-discharge. 
        // This is a safety feature to protect the battery from damage due to over-discharge, 
        // which can occur if the battery is drained below a certain voltage threshold.

        // TODO: Consider implementing hysteresis or a delay before re-enabling charging after load disconnect to prevent rapid on/off 
        // cycling if battery voltage hovers around the threshold.
    }

    m_mpptControl = mpptControl;
    m_actuator->applyControl(mpptControl);
}

bool ChargeController::isChargingAvailable()
{
    return m_pvPowerUnavailableTimer.getDuration() < ChargeControllerConfig::PV_POWER_UNAVAILABLE_TIMEOUT;
}

void ChargeController::handlePvPowerUnavailableTimer(long pvPower_mW)
{
    if(pvPower_mW <= ChargeControllerConfig::PV_POWER_THRESHOLD)
    {
        if(m_pvPower_mW > ChargeControllerConfig::PV_POWER_THRESHOLD)
        {
            ESP_LOGI(TAG, "PV power dropped (%ld mW) — starting unavailable timer", pvPower_mW);
            m_pvPowerUnavailableTimer.trigger();
        }
        else
            m_pvPowerUnavailableTimer.update();
    }
    else
    {
        if (m_pvPower_mW <= ChargeControllerConfig::PV_POWER_THRESHOLD && m_pvPower_mW != 0)
            ESP_LOGI(TAG, "PV power recovered (%ld mW)", pvPower_mW);
        m_pvPowerUnavailableTimer.reset();
    }

    m_pvPower_mW = pvPower_mW;
}

int ChargeController::clampLimit(int measured, int limit, int mpptControl)
{
    if (measured <= 0) return mpptControl;

    int deltaCurrent = measured - limit;
    
    if(deltaCurrent <= 0) return mpptControl;

    int mpptControlCorrection = std::lround((static_cast<double>(mpptControl) / measured) * deltaCurrent);

    return constrain(mpptControl - mpptControlCorrection, 0, 100);
}

int ChargeController::clampLimitPI(int measured, int limit, int mpptControl, long& integralError)
{
    if (measured <= 0) 
        return mpptControl;

    int error = measured - limit;
    
    if(error <= 0) 
    {
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