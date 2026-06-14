#include "PerturbAndObserveMppt.h"
#include "Logger.h"

static constexpr char TAG[] = "PerturbAndObserveMppt";

void PerturbAndObserveMppt::init()
{
    m_control = MIN_CONTROL_VALUE;
    m_controlCollapseCeiling = MAX_CONTROL_VALUE;
    m_voltageAtCeiling_mV = 0;
    m_collapsePointsCandidates.clear();
    m_step = DEFAULT_STEP;
    m_consecutiveMinSteps = 0;
    m_direction = Direction::Up;
    m_pvMeasurements = {}; // Without this, ΔP<0 on first update (power dropped from end-of-cycle to 0 at startup) would flip direction to Down and keep MPPT stuck at 0%.
    m_gradientData = {};
}

void PerturbAndObserveMppt::update(Measurements pvMeasurements)
{
    bool directionFlipped {false};
    int prevConsecutiveMinSteps {m_consecutiveMinSteps};

    long power_mW {pvMeasurements.voltage_mV * pvMeasurements.current_mA / 1000}; // Convert to mW to avoid overflow and match typical PV power units
    long m_power_mW {m_pvMeasurements.voltage_mV * m_pvMeasurements.current_mA / 1000}; // Previous power in mW

    long deltaPower_mW = power_mW - m_power_mW;
    long deltaVoltage_mV = pvMeasurements.voltage_mV - m_pvMeasurements.voltage_mV;
    MpptGradient newGradient{0.0f, false};

    const int voltageCollapseThreshold_mV = m_openCircuitVoltage_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100;

    // Detect voltage collapse: if voltage drops below a percentage of Voc, it's likely we've overshot the MPP and entered the collapse region. 
    // Respond by reducing control aggressively and setting a ceiling to prevent further collapse until we detect conditions have improved (e.g. irradiance increase).
    if(m_openCircuitVoltage_mV > 0 && pvMeasurements.voltage_mV < voltageCollapseThreshold_mV)
    {
        // Only update ceiling and pre-collapse Vin from a valid (non-collapsed) previous measurement.
        if(m_pvMeasurements.voltage_mV > voltageCollapseThreshold_mV)
        {
            auto [collapseControl, collapseVoltage] = m_collapsePointsCandidates.getMinElement().value_or(std::make_pair(MIN_CONTROL_VALUE, 0));
            m_collapsePointsCandidates.clear();
            m_controlCollapseCeiling = constrain(collapseControl, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);
            m_voltageAtCeiling_mV = collapseVoltage;
            ESP_LOGW(TAG, "Voltage dropped below %d%% of Voc (%dmV) — setting collapse ceiling to control=%.2f%% (last safe Vin=%dmV)",
                    PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT, voltageCollapseThreshold_mV, m_controlCollapseCeiling / 10.0, m_pvMeasurements.voltage_mV);
        }
        m_control -= MAX_STEP; // Reduce control by a large step to aviod collapse in case of rapid voltage drop.
        m_control = constrain(m_control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);
        m_pvMeasurements = pvMeasurements; // Update measurements to prevent repeated large steps on every update during a collapse event
        ESP_LOGW(TAG, "Voltage dropped below %d%% of Voc (%dmV) — emergency step to control=%.2f%% (ceiling=%.2f%%)",
                 PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT, voltageCollapseThreshold_mV, m_control / 10.0 , m_controlCollapseCeiling / 10.0);
        return; 
    }

    // Only update gradient and direction if voltage change is large enough to yield a reliable gradient measurement.
    if(std::abs(deltaVoltage_mV) >= MIN_DELTA_VOLTAGE_mV)
    {
        // Scale step proportionally to |dP/dV|, clamped to [MIN_STEP, MAX_STEP]
        newGradient.valid = true;
        newGradient.gradient = std::abs(static_cast<float>(deltaPower_mW) / static_cast<float>(deltaVoltage_mV));
        m_step = constrain(static_cast<int>(K_STEP * newGradient.gradient), MIN_STEP, MAX_STEP);

        // Update direction: if power increased, keep same direction; if power decreased, flip direction
        if(deltaPower_mW < 0)
        {
            m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up;
            directionFlipped = true;
        }

        // Increment counter if we're stepping up at minimum step size, which can indicate we're at the knee; reset otherwise
        (m_step == MIN_STEP && m_direction == Direction::Up) ? m_consecutiveMinSteps++ : m_consecutiveMinSteps = 0;
    }
    else
    {
        newGradient.valid = false;
        ESP_LOGD(TAG, "ΔV (%dmV) below threshold (%dmV) — skipping gradient and direction update to avoid noise, control=%.2f%%",
             std::abs(deltaVoltage_mV), MIN_DELTA_VOLTAGE_mV, m_control / 10.0);
    }

    if(m_direction == Direction::Up)
    {
        // Detect knee: if we're stepping up at minimum step size and voltage drops significantly for several consecutive steps,
        // it's likely we've hit the knee and should set a ceiling to avoid collapse.
        if(deltaVoltage_mV < -KNEE_DELTA_VOLTAGE_THRESHOLD_mV && 
            m_step == MIN_STEP && prevConsecutiveMinSteps >= KNEE_CONSECUTIVE_MIN_STEPS && m_control < m_controlCollapseCeiling)
        {
            m_consecutiveMinSteps = 0; // Reset counter after detecting a knee
            m_collapsePointsCandidates.add({m_control, pvMeasurements.voltage_mV});
            m_controlCollapseCeiling = m_control;
            m_voltageAtCeiling_mV = pvMeasurements.voltage_mV;
            ESP_LOGI(TAG, "ΔVin (%dmV) — potential knee detected, setting collapse ceiling to control=%.2f%% (Vin=%dmV)",
                    std::abs(deltaVoltage_mV), m_controlCollapseCeiling / 10.0, pvMeasurements.voltage_mV);
        }
        else
        {
            m_collapsePointsCandidates.add({m_control, pvMeasurements.voltage_mV});
            m_control = std::min(m_control + m_step, m_controlCollapseCeiling);
        }

        // Detect irradiance increase: if we're hitting the ceiling and Vin is significantly higher than it was just before the last collapse 
        // the old ceiling is no longer valid, reset it.
        if(m_controlCollapseCeiling < MAX_CONTROL_VALUE && m_control == m_controlCollapseCeiling && m_voltageAtCeiling_mV > 0 &&
           pvMeasurements.voltage_mV > m_voltageAtCeiling_mV + IRRADIANCE_INCREASE_VOLTAGE_MARGIN_mV)
        {
            // Same I_SET, higher Vin - irradiance increased
            ESP_LOGI(TAG, "Vin (%dmV) > baseline Vin (%dmV) + %dmV — irradiance increased, clearing ceiling",
                    pvMeasurements.voltage_mV, m_voltageAtCeiling_mV, IRRADIANCE_INCREASE_VOLTAGE_MARGIN_mV);
            m_controlCollapseCeiling = MAX_CONTROL_VALUE;
            m_voltageAtCeiling_mV = 0;
        }
    }
    else
    {
        const int peakControl{m_control};
        m_control -= m_step;

        // Detect knee: if direction flipped to Down and there were several consecutive minimum steps before the flip, it's likely we've hit the knee
        // and should set a ceiling to avoid collapse.
        if(directionFlipped && m_step == MIN_STEP && prevConsecutiveMinSteps >= KNEE_CONSECUTIVE_MIN_STEPS && peakControl < m_controlCollapseCeiling)
        {
            m_consecutiveMinSteps = 0; // Reset counter after detecting a knee
            m_controlCollapseCeiling = peakControl;
            m_voltageAtCeiling_mV = m_pvMeasurements.voltage_mV;
            m_collapsePointsCandidates.add({peakControl, m_pvMeasurements.voltage_mV});
            ESP_LOGI(TAG, "Direction flipped to Down at control=%.2f%% with |dP/dV|=%.3f — potential knee detected, setting collapse ceiling to control=%.2f%% (Vin=%dmV)",
                    peakControl / 10.0, newGradient.gradient, m_controlCollapseCeiling / 10.0, m_voltageAtCeiling_mV);
        }
    }
        
    m_control = constrain(m_control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);

    // Boundary reflection at MIN only: prevents getting permanently stuck at 0% if direction stays Down.
    if (m_control == MIN_CONTROL_VALUE && m_direction == Direction::Down)
        m_direction = Direction::Up;

    m_pvMeasurements = pvMeasurements;
    m_gradientData.gradient = newGradient.gradient;
    m_gradientData.valid = newGradient.valid;

    // Debug output — only log gradient when ΔV is large enough to be meaningful.
    // When ΔV=0 the direction/step logic was skipped; log "n/a" to avoid nan/inf.
    if (newGradient.valid)
        ESP_LOGD(TAG, "|dP/dV|=%.3f, step=%d, direction=%s, control=%.2f%%",
                 newGradient.gradient, m_step,
                 (m_direction == Direction::Up) ? "Up" : "Down",
                 m_control / 10.0);
    else
        ESP_LOGD(TAG, "|dP/dV|=n/a (dV=0), step=%d, direction=%s, control=%.2f%%",
                 m_step,
                 (m_direction == Direction::Up) ? "Up" : "Down",
                 m_control / 10.0);
}

void PerturbAndObserveMppt::setOpenCircuitVoltage(int openCircuitVoltage_mV)
{
    if(m_openCircuitVoltage_mV != openCircuitVoltage_mV && openCircuitVoltage_mV > 0)
    {
        m_controlCollapseCeiling = MAX_CONTROL_VALUE; // Reset collapse ceiling when Voc changes
        m_voltageAtCeiling_mV = 0;
        m_collapsePointsCandidates.clear();
        m_openCircuitVoltage_mV = openCircuitVoltage_mV;
        ESP_LOGI(TAG, "Open-circuit voltage set to %dmV", m_openCircuitVoltage_mV);
    }
}

void PerturbAndObserveMppt::syncControl(int control)
{ 
    if(control != m_control && control >= MIN_CONTROL_VALUE && control <= MAX_CONTROL_VALUE)
    {
        ESP_LOGI(TAG, "Syncing internal control from %.2f%% to %.2f%% to match actual control being applied by ChargeController",
                 m_control / 10.0, control / 10.0);
        m_control = control;
    }
}