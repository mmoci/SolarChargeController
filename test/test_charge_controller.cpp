#include <gtest/gtest.h>
#include "ChargeController.h"
#include "MockMeasurements.h"
#include "MockActuator.h"
#include "Config.h"

class ChargeControllerTest : public ::testing::Test
{
protected:
    MockMeasurements pvMeasurements;
    MockMeasurements batteryMeasurements;
    MockActuator actuator;
    ChargeController controller{&pvMeasurements, &batteryMeasurements, &actuator};
    
    void SetUp() override
    {
        controller.init();
        
        // Set reasonable initial values
        pvMeasurements.setVoltage_mV(18000);
        pvMeasurements.setCurrent_mA(5000);
        batteryMeasurements.setVoltage_mV(11000);
        batteryMeasurements.setCurrent_mA(1000);
    }
};

// Test initialization
TEST_F(ChargeControllerTest, InitializesSuccessfully)
{
    // Should not throw during init
    EXPECT_NO_FATAL_FAILURE({
        controller.init();
    });
}

// Test basic charging operation
TEST_F(ChargeControllerTest, BasicChargingOperation)
{
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    batteryMeasurements.setVoltage_mV(11000);
    batteryMeasurements.setCurrent_mA(1000);
    
    controller.update();
    
    // Should apply some control to the actuator
    int lastControl = actuator.getLastControl();
    EXPECT_NE(lastControl, -1);
}

// Test stops charging when PV power unavailable (time-based)
TEST_F(ChargeControllerTest, StopsChargingWhenPVPowerUnavailable)
{
    reset_millis();

    // Establish CC mode with high PV power (>1W threshold)
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);  // 90W
    controller.update();
    ASSERT_EQ(controller.getBatteryMode(), BatteryManager::Mode::CC);

    // PV power drops below 1W threshold at millis()=1 — triggers pvPowerUnavailableTimer.
    // ChargeController converts voltage_mV * current_mA / 1000 → mW before comparing
    // against PV_POWER_THRESHOLD = 1000 mW (1 W). Values must produce < 1000 mW.
    advance_millis(1);
    pvMeasurements.setVoltage_mV(1000);
    pvMeasurements.setCurrent_mA(500);   // 1000*500/1000 = 500 mW < threshold 1000 mW
    controller.update();

    // Still charging at 9s: timer not yet expired (needs 10s)
    advance_millis(9000);
    controller.update();
    EXPECT_EQ(controller.getBatteryMode(), BatteryManager::Mode::CC);

    // PV unavailable at 10002ms: isPvAvailable() = false — BatteryManager sees false
    // This triggers the 60s charging-disabled timer in BatteryManager
    advance_millis(1001);
    controller.update();

    // Advance past 60s charging-disabled timeout — CC transitions to Idle
    advance_millis(60001);
    controller.update();
    EXPECT_EQ(controller.getBatteryMode(), BatteryManager::Mode::Idle);
    EXPECT_EQ(actuator.getLastControl(), 0);
}

// Test handles stale PV measurements
TEST_F(ChargeControllerTest, HandlesStaleVmeasurements)
{
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    pvMeasurements.setValid(false);  // Mark PV measurements as stale
    batteryMeasurements.setVoltage_mV(11000);
    batteryMeasurements.setCurrent_mA(1000);
    
    // Should not crash and should maintain control
    EXPECT_NO_FATAL_FAILURE({
        controller.update();
    });
}

// Test handles stale battery measurements
TEST_F(ChargeControllerTest, HandlesStaleBatteryMeasurements)
{
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    batteryMeasurements.setVoltage_mV(11000);
    batteryMeasurements.setCurrent_mA(1000);
    batteryMeasurements.setValid(false);  // Mark battery measurements as stale
    
    // Should not crash and should reduce control
    EXPECT_NO_FATAL_FAILURE({
        controller.update();
    });
}

// Test soft ramping of control value
TEST_F(ChargeControllerTest, ControlRampsSmootly)
{
    std::vector<int> controlValues;
    
    // Start with low voltage to slow charging
    pvMeasurements.setVoltage_mV(15000);
    pvMeasurements.setCurrent_mA(3000);
    
    for(int iteration = 0; iteration < 20; ++iteration)
    {
        // Gradually increase PV power
        pvMeasurements.setVoltage_mV(15000 + iteration * 200);
        pvMeasurements.setCurrent_mA(3000 + iteration * 100);
        
        controller.update();
        controlValues.push_back(actuator.getLastControl());
    }
    
    // Check that control changes are smooth (no large jumps)
    for(size_t i = 1; i < controlValues.size(); ++i)
    {
        int delta = std::abs(controlValues[i] - controlValues[i-1]);
        EXPECT_LE(delta, actuator.getMaxSoftStep() + 1);
    }
}

// Test respects battery manager limits
TEST_F(ChargeControllerTest, RespectsBatteryLimits)
{
    // Set battery to high voltage (CV mode)
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    batteryMeasurements.setVoltage_mV(12600);  // Max voltage
    batteryMeasurements.setCurrent_mA(1000);
    
    for(int i = 0; i < 5; ++i)
    {
        controller.update();
    }
    
    // Should apply control but respect battery manager
    int lastControl = actuator.getLastControl();
    EXPECT_LE(lastControl, 100);
}

// Test disables charging when battery full
TEST_F(ChargeControllerTest, DisablesChargingWhenBatteryFull)
{
    // Set battery to charged state (done charging): at max voltage with current below cutoff
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    batteryMeasurements.setVoltage_mV(12600);  // maxVoltage_mV
    batteryMeasurements.setCurrent_mA(50);     // below cutoffCurrent_mA (100mA)
    
    // Idle -> CC -> CV -> Done requires 3 updates:
    for(int i = 0; i < 5; ++i)
        controller.update();
    
    // Done mode: isChargingAllowed() = false -> applyControl(0)
    EXPECT_EQ(controller.getBatteryMode(), BatteryManager::Mode::Done);
    EXPECT_EQ(actuator.getLastControl(), 0);
}

// Test zero control with zero PV power
TEST_F(ChargeControllerTest, ZeroControlWithZeroPvPower)
{
    pvMeasurements.setVoltage_mV(0);
    pvMeasurements.setCurrent_mA(0);
    batteryMeasurements.setVoltage_mV(11000);
    batteryMeasurements.setCurrent_mA(0);
    
    // Should handle zero values without crashing
    EXPECT_NO_FATAL_FAILURE({
        for(int i = 0; i < 5; ++i)
        {
            controller.update();
        }
    });
}

// Test rapid changes in PV conditions
TEST_F(ChargeControllerTest, HandlesRapidPvChanges)
{
    // Simulate rapid PV voltage fluctuations
    for(int i = 0; i < 20; ++i)
    {
        pvMeasurements.setVoltage_mV(15000 + (i % 5) * 1000);
        pvMeasurements.setCurrent_mA(3000 + (i % 5) * 500);
        
        EXPECT_NO_FATAL_FAILURE({
            controller.update();
        });
    }
}

// Test MPPT only perturbs when measurementAge reports a new hardware read
TEST_F(ChargeControllerTest, MpptOnlyPerturbs_WhenNewMeasurementArrives)
{
    // Static timestamp: same measurement repeated (no new hardware read)
    pvMeasurements.setLastUpdate(100);
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);

    // First call: m_lastPvUpdateAge==0 -> perturbs unconditionally, sets m_lastPvUpdateAge=100
    controller.update();
    int controlAfterFirst = actuator.getLastControl();

    // Second call: pvUpdateAge=100, m_lastPvUpdateAge=100 -> 100<100 false, 100==0 false -> no perturb
    controller.update();
    EXPECT_EQ(actuator.getLastControl(), controlAfterFirst);

    // New measurement arrived: pvUpdateAge=5 < m_lastPvUpdateAge=100 -> perturbs
    pvMeasurements.setLastUpdate(5);
    controller.update();
    EXPECT_NE(actuator.getLastControl(), controlAfterFirst);
}

// Test that BatteryManager is not updated when battery measurements are stale.
// On startup (e.g. DPS not yet responding) Vbatt=0mV would trip minSafeVoltage
// and put BatteryManager into the terminal Fault state permanently.
TEST_F(ChargeControllerTest, DoesNotFaultWhenBatteryMeasurementsStale)
{
    batteryMeasurements.setVoltage_mV(0);   // would trip fault if passed through
    batteryMeasurements.setCurrent_mA(0);
    batteryMeasurements.setValid(false);    // stale — no hardware response yet

    for(int i = 0; i < 5; ++i)
        controller.update();

    EXPECT_NE(controller.getBatteryMode(), BatteryManager::Mode::Fault);
    EXPECT_EQ(controller.getBatteryMode(), BatteryManager::Mode::Idle);
}

// Test that BatteryManager is not updated when BOTH PV and battery measurements are
// stale simultaneously (e.g. DPS device not yet responding at startup).
// Both pvValid and batteryValid are false — neither guard passes Vbatt=0 to
// BatteryManager, so no Fault is triggered before real voltage is observed.
TEST_F(ChargeControllerTest, DoesNotFaultWhenBothMeasurementsStale)
{
    batteryMeasurements.setVoltage_mV(0);   // would trip fault if passed through
    batteryMeasurements.setCurrent_mA(0);
    batteryMeasurements.setValid(false);    // stale — DPS not yet responding
    pvMeasurements.setVoltage_mV(0);
    pvMeasurements.setCurrent_mA(0);
    pvMeasurements.setValid(false);         // also stale — same DPS source

    for(int i = 0; i < 5; ++i)
        controller.update();

    EXPECT_NE(controller.getBatteryMode(), BatteryManager::Mode::Fault);
    EXPECT_EQ(controller.getBatteryMode(), BatteryManager::Mode::Idle);
}

// Test PI clamps (applyLimitConstraints) do NOT advance when measurements are
// valid but not updated this cycle (State::NotUpdated). Without this gate the PI
// integrator accumulates at loop rate (~3ms) on stale data instead of Modbus
// rate (~900ms), which drives mpptControl to 0 in ~9ms on first CV entry.
TEST_F(ChargeControllerTest, PiClamps_DoNotAdvance_WhenMeasurementsNotUpdated)
{
    pvMeasurements.setVoltage_mV(18000);
    pvMeasurements.setCurrent_mA(5000);
    batteryMeasurements.setVoltage_mV(12600);  // maxVoltage_mV — triggers CV
    batteryMeasurements.setCurrent_mA(500);    // above cutoff (100mA) — stay in CV

    // setLastUpdate(0): age=0, lastMeasurementAge resets to 0 each call, so
    // (0 < 0 || 0 == 0) is always true — every update returns Updated state.
    // Run enough cycles to reach CV mode (Idle→CC→CV takes ~2-3 updates).
    pvMeasurements.setLastUpdate(0);
    for (int i = 0; i < 5; ++i)
        controller.update();
    ASSERT_EQ(controller.getBatteryMode(), BatteryManager::Mode::CV);

    // Freeze age at 100ms. First call: lastMeasurementAge was 0 (from above) →
    // (100 < 0 || 0 == 0) = true → Updated, PI runs, establishes baseline setpoint.
    pvMeasurements.setLastUpdate(100);
    controller.update();
    const int controlAfterFirstUpdate = actuator.getLastControl();

    // Subsequent calls: age=100, lastMeasurementAge=100 → NotUpdated, PI must NOT advance.
    // If the gate is broken, repeated voltage PI corrections would drive control toward 0.
    for (int i = 0; i < 10; ++i)
        controller.update();

    EXPECT_EQ(actuator.getLastControl(), controlAfterFirstUpdate);
}
