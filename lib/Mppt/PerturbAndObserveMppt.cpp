#include "PerturbAndObserveMppt.h"
#include "Logger.h"

static constexpr char TAG[] = "PerturbAndObserveMppt";

void PerturbAndObserveMppt::init()
{
    m_control = MIN_CONTROL_VALUE;
    m_step = DEFAULT_STEP;
    m_direction = Direction::Up;
    // Clear stored measurements so the first update after reset computes delta against {0,0} rather than stale data from a
    // previous charge cycle. Without this, ΔP<0 on first update (power dropped from end-of-cycle to 0 at startup) would
    // flip direction to Down and keep MPPT stuck at 0%.
    m_pvMeasurements = {};
}

void PerturbAndObserveMppt::update(Measurements pvMeasurements)
{
    long power_mW {pvMeasurements.voltage_mV * pvMeasurements.current_mA / 1000}; // Convert to mW to avoid overflow and match typical PV power units
    long m_power_mW {m_pvMeasurements.voltage_mV * m_pvMeasurements.current_mA / 1000}; // Previous power in mW

    long deltaPower_mW = power_mW - m_power_mW;
    long deltaVoltage_mV = pvMeasurements.voltage_mV - m_pvMeasurements.voltage_mV;

    float gradient = 0.0f;
    bool gradientValid = false;

    if(std::abs(deltaVoltage_mV) >= MIN_DELTA_VOLTAGE_mV)
    {
        // Scale step proportionally to |dP/dV|, clamped to [MIN_STEP, MAX_STEP]
        gradientValid = true;
        gradient = std::abs(static_cast<float>(deltaPower_mW) / static_cast<float>(deltaVoltage_mV));
        m_step = constrain(static_cast<int>(K_STEP * gradient), MIN_STEP, MAX_STEP);

        if(deltaPower_mW < 0)
            m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up;
    }

    if(m_direction == Direction::Up)
        m_control += m_step;
    else
        m_control -= m_step;

    m_control = constrain(m_control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);

    // Boundary reflection: if control has been clamped to a limit while direction still points toward it, reverse direction immediately.
    if (m_control == MIN_CONTROL_VALUE && m_direction == Direction::Down)
        m_direction = Direction::Up;
    else if (m_control == MAX_CONTROL_VALUE && m_direction == Direction::Up)
        m_direction = Direction::Down;

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