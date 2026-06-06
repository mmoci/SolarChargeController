#pragma once

#include "DPSxDcConverter.h"
#include "MeasurementsIf.h"

class DPSxMeasurements : public MeasurementsIf
{
    public:
    // DPS Modbus cycle is ~450ms per message. When a write is pending, up to
    // MAX_READS_BEFORE_WRITE+1 cycles pass without a READ (= ~1.8s gap).
    // The stale timeout must exceed the worst-case gap between reads.
    static constexpr uint32_t MEASUREMENT_STALE_TIMEOUT_MS {2000}; // 2s — covers up to 4 Modbus cycles without a READ

    enum class MeasurementSource
    {
        Input,
        Output
    };
    
    DPSxMeasurements(DPSxDcConverter* dpsConverter, MeasurementSource measSource) :
        p_dpsConverter{dpsConverter},
        m_measSource{measSource}
    {}

    int getVoltage_mV() const override 
    {
        return (m_measSource == MeasurementSource::Input) ? p_dpsConverter->getInVoltage_mV() : p_dpsConverter->getVoltage_mV();
    };

    std::optional<int> getOpenCircuitVoltage_mV() const override
    {
        return p_dpsConverter->getOpenCircuitVoltage_mV();
    }

    /**
     * On DPS5005 in series circuit topology:
     * - Input current == Output current (Kirchhoff's law)
     * - This adapter always returns the same value for both
     * - NOT the input-side only current
     */
    int getCurrent_mA() const override 
    {
        // Both input and output report the same current on DPS (series connection)
        return p_dpsConverter->getCurrent_mA();
    };

    bool isMeasurementValid() const override
    {
        // Check if DPS device has valid measurements
        if (!p_dpsConverter->hasMeasurements())
            return false;

        return measurementAge() < MEASUREMENT_STALE_TIMEOUT_MS;
    }

    unsigned long measurementAge() const override 
    {
        return millis() - p_dpsConverter->getLastUpdateTime();
    }

    bool isMeasurementUpdated() override
    {
        const unsigned long age = measurementAge();
        const bool updated = (age < m_lastMeasurementAge || m_lastMeasurementAge == 0);
        m_lastMeasurementAge = age; // always update so the next detection reliably sees a large previous value
        return updated;
    }

    private:
    DPSxDcConverter* p_dpsConverter;
    MeasurementSource m_measSource{};
    unsigned long m_lastMeasurementAge{};
};