This is implementation of ESP32 based MPPT solar charge controller.

ChargeController
│
├── Sensors (composition)
│   ├── pvVoltage     : VoltageSensor
│   ├── pvCurrent     : CurrentSensor
│   ├── battVoltage   : VoltageSensor
│   ├── battCurrent   : CurrentSensor
│
├── MpptController     (pure algorithm, stateless or small state)
│
├── BatteryManager
│   ├── BatteryProfile (limits, chemistry rules)
│   └── BatteryState   (SOC, phase, flags)
│
└── DcDcConverter      (PWM actuator)

