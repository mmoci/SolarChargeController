#pragma once
#include "SensorINA226.h"
#include "MpptController.h"
#include "BatteryManager.h"
#include "DcConverter.h"

class ChargeController
{
    public:
    ChargeController(uint16_t pvSensorAddress, uint16_t batterySensorAddress, uint8_t pwmPin) :  
        m_pvSensor{pvSensorAddress}, m_batterySensor{batterySensorAddress}, m_dcConverter{pwmPin}
    {}

    void init();
    void update();

    private:
    SensorINA226 m_pvSensor;
    SensorINA226 m_batterySensor;
    DcConverter m_dcConverter;
    BatteryManager m_batteryManager{};
    MpptController m_mpptController{};

    long m_pvPower_mW{};
    Timer m_pvPowerUnavailableTimer{};

    bool isChargingAvailable();
    void handlePvPowerUnavailableTimer(long pvPower_mW);
    int clampCurrentLimit(int batteryCurrent, int currentLimit, int pwmDuty);
    int clampVoltageLimit(int batteryVoltage, int voltageLimit, int pwmDuty);
};