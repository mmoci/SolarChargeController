#pragma once

#include "DPSxDcConverter.h"
#include "MeasurementsIf.h"

class DPSxMeasurements : public MeasurementsIf
{
    public:
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

    private:
    DPSxDcConverter* p_dpsConverter;
    MeasurementSource m_measSource{};
};