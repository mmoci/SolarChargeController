#include "InputVoltageRegulationMppt.h"
#include "Config.h"
#include "Logger.h"

static constexpr char TAG[] = "InputVoltageRegulationMppt";

void InputVoltageRegulationMppt::init()
{
    m_control = MIN_CONTROL_VALUE;
    m_openCircuitVoltage_mV = PvArrayConfig::DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV;
    m_mpptVoltage = (m_openCircuitVoltage_mV * PvArrayConfig::INITIAL_MPPT_VOLTAGE_PERCENT) / 100;
    m_pvMeasurements = {};
}

void InputVoltageRegulationMppt::update(Measurements pvMeasurements)
{
    m_pvMeasurements = pvMeasurements;

    if (m_mpptVoltage <= 0)
        return; // No valid Vmpp target yet — hold at current control value

    // Proportional controller: regulate panel input voltage to Vmpp = 0.76 * Voc.
    // Positive error (Vin > Vmpp): panel is above MPP knee → draw more current to pull Vin down.
    // Negative error (Vin < Vmpp): panel is below MPP knee → draw less current to let Vin recover.
    // Increasing I_SET draws more current from the panel, causing Vin to drop along the I-V curve —
    // this is the negative feedback that makes IVR inherently stable, unlike P&O on a CC source.
    const int error_mV = pvMeasurements.voltage_mV - m_mpptVoltage;
    const int correction = static_cast<int>(K_P * error_mV);
    m_control = constrain(m_control + correction, MIN_CONTROL_VALUE, MAX_CONTROL_VALUE);

    ESP_LOGD(TAG, "Vin=%dmV Vmpp=%dmV err=%dmV corr=%d ctrl=%d%%",
             pvMeasurements.voltage_mV, m_mpptVoltage, error_mV, correction, m_control);
}

void InputVoltageRegulationMppt::setOpenCircuitVoltage(int openCircuitVoltage_mV)
{
    if(m_openCircuitVoltage_mV != openCircuitVoltage_mV && openCircuitVoltage_mV > 0)
    {
        m_openCircuitVoltage_mV = openCircuitVoltage_mV;
        m_mpptVoltage = (m_openCircuitVoltage_mV * PvArrayConfig::INITIAL_MPPT_VOLTAGE_PERCENT) / 100;
        m_control = MIN_CONTROL_VALUE; // Reset control to minimum when OCV changes to avoid overshooting the new Vmpp target
        ESP_LOGI(TAG, "Open-circuit voltage set to %dmV, MPPT voltage target updated to %dmV", m_openCircuitVoltage_mV, m_mpptVoltage);
    }
}