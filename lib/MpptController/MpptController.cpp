#include "MpptController.h"
#include "Logger.h"

static constexpr char TAG[] = "MpptController";

void MpptController::init()
{
    m_outputData.control = MIN_CONTROL_VALUE;
    m_step = DEFAULT_STEP;
    m_direction = Direction::Up;
    // Clear stored measurements so the first update after reset computes delta against {0,0} rather than stale data from a
    // previous charge cycle. Without this, ΔP<0 on first update (power dropped from end-of-cycle to 0 at startup) would
    // flip direction to Down and keep MPPT stuck at 0%.
    m_pvMeasurements = {};
    m_collapsedTimer.reset();
    m_isCollapsing = false;
    m_collapseLimitData.control = MAX_CONTROL_VALUE;
    m_lastCeilingRelaxTime = 0;
}

void MpptController::update(Measurements pvMeasurements)
{
    // Edge-trigger: flag is true for exactly the one MPPT cycle in which a collapse is detected.
    // Cleared here at the start so the caller sees it for the full duration of this cycle regardless
    // of when isPvPowerCollapsing() is called, and it auto-resets without any external dependency.
    m_isCollapsing = false;

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

        if(m_collapsedTimer.active())
        {
            // Stabilization window: DPS is recovering from a collapse. Suppress P&O direction changes
            // so we don't make algorithm decisions on noisy/transient measurements.
            // The ceiling enforced below keeps control from climbing back to the cliff.
            m_collapsedTimer.update();
            if(m_collapsedTimer.getDuration() >= COLLAPSE_DURATION)
            {
                m_collapsedTimer.reset();
                ESP_LOGD(TAG, "Collapse stabilization window expired — resuming normal P&O");
            }
        }
        else if(deltaPower_mW < 0)
        {
            // Normal P&O: power dropped. Distinguish a genuine collapse (huge |dP/dV|) from a
            // normal overshoot (small |dP/dV|) and handle each differently.
            if(deltaVoltage_mV < -static_cast<long>(COLLAPSE_VOLTAGE_DROP_THRESHOLD_mV))
            {
                // Collapse: Vin crashed by > COLLAPSE_VOLTAGE_DROP_THRESHOLD_mV in one P&O step.
                // Back off control and signal ChargeController to cycle the DPS output
                // (OFF → reduced I_SET → ON) to reset its regulation loop.
                m_collapsedTimer.trigger();
                m_isCollapsing = true;
                m_outputData.control = m_outputData.control * BACKOFF_COLLAPSE_FACTOR / 100;
                m_collapseLimitData.control = m_outputData.control; // Ceiling = backed-off value
                m_lastCeilingRelaxTime = millis();
                ESP_LOGW(TAG, "PV collapse detected (dV=%ldmV) — backing off to %d%%, ceiling set",
                         deltaVoltage_mV, m_outputData.control);
            }
            else
            {
                m_direction = (m_direction == Direction::Up) ? Direction::Down : Direction::Up;
            }
        }
    }

    if(m_direction == Direction::Up)
        m_outputData.control += m_step;
    else
        m_outputData.control -= m_step;

    m_outputData.control = constrain(m_outputData.control, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);

    // Enforce collapse ceiling: prevents MPPT from climbing straight back to the cliff after recovery.
    // The ceiling is set to the backed-off value on collapse and relaxed gradually (see below).
    m_outputData.control = std::min(m_outputData.control, m_collapseLimitData.control);

    // Relax ceiling by CEILING_RELAX_STEP every CEILING_RELAX_INTERVAL_MS so the tracker can
    // adapt to improving irradiance without immediately re-exposing the cliff.
    // When the ceiling reaches MAX_CONTROL_VALUE it is effectively inactive.
    if(m_collapseLimitData.control < MAX_CONTROL_VALUE)
    {
        const unsigned long now = millis();
        if(now - m_lastCeilingRelaxTime >= CEILING_RELAX_INTERVAL_MS)
        {
            m_collapseLimitData.control = std::min(m_collapseLimitData.control + CEILING_RELAX_STEP, MAX_CONTROL_VALUE);
            m_lastCeilingRelaxTime = now;
            ESP_LOGD(TAG, "Collapse ceiling relaxed to %d%%", m_collapseLimitData.control);
        }
    }

    // Boundary reflection: if control has been clamped to a limit while direction still
    // points toward it, reverse direction immediately.
    // Without this, a write-lag-induced false direction flip drives control to 0% (or 100%)
    // and permanently strands MPPT there — at the boundary ΔV≈0 because the source no
    // longer responds (PSU in CV, real panel fully loaded/unloaded), so no natural gradient
    // forms to flip direction back via the normal P&O path.
    if (m_outputData.control == MIN_CONTROL_VALUE && m_direction == Direction::Down)
        m_direction = Direction::Up;
    else if (m_outputData.control == MAX_CONTROL_VALUE && m_direction == Direction::Up)
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
                 m_outputData.control);
    else
        ESP_LOGD(TAG, "|dP/dV|=n/a (dV=0), step=%d, direction=%s, control=%d%%",
                 m_step,
                 (m_direction == Direction::Up) ? "Up" : "Down",
                 m_outputData.control);
}