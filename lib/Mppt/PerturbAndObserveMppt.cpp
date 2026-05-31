#include "PerturbAndObserveMppt.h"
#include "Logger.h"

static constexpr char TAG[] = "PerturbAndObserveMppt";

void PerturbAndObserveMppt::init()
{
    m_control = MIN_CONTROL_VALUE;
    m_controlCollapseCeiling = MAX_CONTROL_VALUE;
    m_voltageAtCeiling_mV = 0;
    m_step = DEFAULT_STEP;
    m_direction = Direction::Up;
    m_pvMeasurements = {}; // Without this, ΔP<0 on first update (power dropped from end-of-cycle to 0 at startup) would flip direction to Down and keep MPPT stuck at 0%.
    m_gradientData = {};
}

void PerturbAndObserveMppt::update(Measurements pvMeasurements)
{
    long power_mW {pvMeasurements.voltage_mV * pvMeasurements.current_mA / 1000}; // Convert to mW to avoid overflow and match typical PV power units
    long m_power_mW {m_pvMeasurements.voltage_mV * m_pvMeasurements.current_mA / 1000}; // Previous power in mW

    long deltaPower_mW = power_mW - m_power_mW;
    long deltaVoltage_mV = pvMeasurements.voltage_mV - m_pvMeasurements.voltage_mV;

    float gradient {0.0f};
    bool gradientValid {false};

    const int voltageCollapseThreshold_mV = m_openCircuitVoltage_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100;
    if(m_openCircuitVoltage_mV > 0 && pvMeasurements.voltage_mV < voltageCollapseThreshold_mV)
    {
        // Only update ceiling and pre-collapse Vin from a valid (non-collapsed) previous measurement.
        if(m_pvMeasurements.voltage_mV > voltageCollapseThreshold_mV)
        {
            m_controlCollapseCeiling = constrain(m_control - 1, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE); // One step below the collapse point — the last known safe value
            m_voltageAtCeiling_mV = m_pvMeasurements.voltage_mV; // Vin at ceiling control; baseline for irradiance increase detection
            ESP_LOGW(TAG, "Voltage dropped below %d%% of Voc (%dmV) — setting collapse ceiling to control=%d%% at ceiling Vin=%dmV",
                     PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT, voltageCollapseThreshold_mV, m_controlCollapseCeiling, m_voltageAtCeiling_mV);
        }
        m_control -= MAX_STEP; // Reduce control by a large step to aviod collapse in case of rapid voltage drop.
        m_control = constrain(m_control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);
        m_pvMeasurements = pvMeasurements; // Update measurements to prevent repeated large steps on every update during a collapse event
        ESP_LOGW(TAG, "Voltage dropped below %d%% of Voc (%dmV) — emergency step to control=%d%% (ceiling=%d%%)",
                 PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT, voltageCollapseThreshold_mV, m_control, m_controlCollapseCeiling);
        return; // Skip normal P&O logic during a collapse event to avoid overreacting to noise in the critical low-voltage region
    }

    if(std::abs(deltaVoltage_mV) >= MIN_DELTA_VOLTAGE_mV)
    {
        // Scale step proportionally to |dP/dV|, clamped to [MIN_STEP, MAX_STEP]
        gradientValid = true;
        gradient = std::abs(static_cast<float>(deltaPower_mW) / static_cast<float>(deltaVoltage_mV));
        m_step = constrain(static_cast<int>(K_STEP * gradient), MIN_STEP, MAX_STEP);

        if(deltaPower_mW < 0)
            m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up;
    }
    else
    {
        // ΔV too small for reliable gradient — skip gradient/direction update but still apply current step
        // m_pvMeasurements is updated unconditionally at the end, preventing gradient spikes later
    }

    if(m_direction == Direction::Up)
    {
        m_control = std::min(m_control + m_step, m_controlCollapseCeiling);
        // Detect irradiance increase: if we're hitting the ceiling and Vin is significantly
        // higher than it was just before the last collapse (same I_SET, higher voltage = panel
        // can deliver more), the old ceiling is no longer valid.
        if(m_controlCollapseCeiling < MAX_CONTROL_VALUE && m_control == m_controlCollapseCeiling &&
           m_voltageAtCeiling_mV > 0 && pvMeasurements.voltage_mV > m_voltageAtCeiling_mV + IRRADIANCE_INCREASE_VOLTAGE_MARGIN_mV)
        {
            ESP_LOGI(TAG, "Vin (%dmV) > pre-collapse Vin (%dmV) + %dmV margin — irradiance increased, clearing collapse ceiling",
                     pvMeasurements.voltage_mV, m_voltageAtCeiling_mV, IRRADIANCE_INCREASE_VOLTAGE_MARGIN_mV);
            m_controlCollapseCeiling = MAX_CONTROL_VALUE;
            m_voltageAtCeiling_mV = 0;
        }
    }
    else
        m_control -= m_step;

    m_control = constrain(m_control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);

    // Boundary reflection at MIN only: prevents getting permanently stuck at 0% if direction stays Down.
    if (m_control == MIN_CONTROL_VALUE && m_direction == Direction::Down)
        m_direction = Direction::Up;

    m_pvMeasurements = pvMeasurements;
    m_gradientData.gradient = gradient;
    m_gradientData.valid = gradientValid;

    // Debug output — only log gradient when ΔV is large enough to be meaningful.
    // When ΔV=0 the direction/step logic was skipped; log "n/a" to avoid nan/inf.
    if (gradientValid)
        ESP_LOGD(TAG, "|dP/dV|=%.3f, step=%d, direction=%s, control=%d%%",
                 gradient, m_step,
                 (m_direction == Direction::Up) ? "Up" : "Down",
                 m_control);
    else
        ESP_LOGD(TAG, "|dP/dV|=n/a (dV=0), step=%d, direction=%s, control=%d%%",
                 m_step,
                 (m_direction == Direction::Up) ? "Up" : "Down",
                 m_control);
}

void PerturbAndObserveMppt::setOpenCircuitVoltage(int openCircuitVoltage_mV)
{
    if(m_openCircuitVoltage_mV != openCircuitVoltage_mV && openCircuitVoltage_mV > 0)
    {
        m_controlCollapseCeiling = MAX_CONTROL_VALUE; // Reset collapse ceiling when Voc changes
        m_voltageAtCeiling_mV = 0;
        m_openCircuitVoltage_mV = openCircuitVoltage_mV;
        ESP_LOGI(TAG, "Open-circuit voltage set to %dmV", m_openCircuitVoltage_mV);
    }
}