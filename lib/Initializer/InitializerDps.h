#pragma once

#include "Initializer.h"
#include "DPSxMeasurements.h"
#include "InputVoltageRegulationMppt.h"

class InitializerDps : public Initializer
{
    public:
    void init() override;
    void update() override;
    MeasurementsIf& getPvMeasurements() override;
    MeasurementsIf& getBatteryMeasurements() override;
    ActuatorIf& getActuator() override;
    MpptStrategyIf& getMpptStrategy() override;

    private:
    static constexpr uint8_t SERIAL2_RX_PIN {16};
    static constexpr uint8_t SERIAL2_TX_PIN {17};

    DPSxDcConverter dpsDcConverter{};
    DPSxMeasurements pvMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Input};
    DPSxMeasurements batteryMeasurements{&dpsDcConverter, DPSxMeasurements::MeasurementSource::Output};
    InputVoltageRegulationMppt dpsMpptStrategy{};
};
    