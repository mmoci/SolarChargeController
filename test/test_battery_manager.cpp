#include <gtest/gtest.h>
#include "BatteryManager.h"
#include "Config.h"
#include "MockMeasurements.h"

class BatteryManagerTest : public ::testing::Test
{
protected:
    BatteryManager batteryManager;
    
    void SetUp() override
    {
        // Initialize with default LI_ION_3S profile
        batteryManager.init(BatteryConfig::LI_ION_3S_DEFAULT);
    }
};

// Test initial state
TEST_F(BatteryManagerTest, InitialStateIsIdle)
{
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Idle);
    EXPECT_FALSE(batteryManager.isChargingAllowed());
    EXPECT_FALSE(batteryManager.isVoltageLimitActive());
    EXPECT_FALSE(batteryManager.isCurrentLimitActive());
}

// Test transition from Idle to Precharge mode
TEST_F(BatteryManagerTest, IdleToPrechargeTransition)
{
    // Precharge voltage is 9600mV, so voltage must be BELOW 9600 and above 9000 (min safe) for Precharge
    Measurements measurements{9500, 1000};  // 9.5V (between min safe 9V and precharge 9.6V)
    
    batteryManager.update(measurements, true);  // Charging available
    
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Precharge);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
    EXPECT_TRUE(batteryManager.isCurrentLimitActive());
}

// Test transition from Idle to CC mode
TEST_F(BatteryManagerTest, IdleToConstantCurrentTransition)
{
    Measurements measurements{11000, 1000};  // 11V, 1A (above precharge voltage)
    
    batteryManager.update(measurements, true);  // Charging available
    
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
    EXPECT_TRUE(batteryManager.isCurrentLimitActive());
}

// Test transition from CC to CV mode
TEST_F(BatteryManagerTest, CcToConstantVoltageTransition)
{
    // First move to CC mode
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);
    
    // Now move to CV mode by increasing voltage above max voltage
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);
    
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
    EXPECT_TRUE(batteryManager.isVoltageLimitActive());
}

// Test that charging is disabled when no power available
TEST_F(BatteryManagerTest, ChargingRemainsAllowedImmediatelyAfterPowerLoss)
{
    Measurements measurements{11000, 1000};
    
    // First update with charging available
    batteryManager.update(measurements, true);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
    
    // Update with charging not available — still allowed immediately (timer not expired)
    batteryManager.update(measurements, false);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
}

// Test fault condition when voltage too low
TEST_F(BatteryManagerTest, FaultModeOnLowVoltage)
{
    Measurements measurements{8000, 1000};  // Below min safe voltage
    
    batteryManager.update(measurements, true);
    
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Fault);
    EXPECT_FALSE(batteryManager.isChargingAllowed());
}

// Test load disconnect voltage check
TEST_F(BatteryManagerTest, LoadDisconnectVoltageCheck)
{
    int loadDisconnectVoltage = 9600;  // From config
    
    EXPECT_TRUE(batteryManager.isLoadDisconnectVoltageLimitActive(9500));
    EXPECT_FALSE(batteryManager.isLoadDisconnectVoltageLimitActive(9600));
    EXPECT_FALSE(batteryManager.isLoadDisconnectVoltageLimitActive(10000));
}

// Test max voltage limit getter
TEST_F(BatteryManagerTest, MaxVoltageLimitInCVMode)
{
    // First move to CC mode
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);
    
    // Then move to CV mode by reaching max voltage
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);
    
    auto limit = batteryManager.getMaxVoltageLimit();
    EXPECT_TRUE(limit.has_value());
    EXPECT_EQ(limit.value(), 12600);
}

// Test max current limit getter in CC mode
TEST_F(BatteryManagerTest, MaxCurrentLimitInCCMode)
{
    // Move to CC mode
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    
    auto limit = batteryManager.getMaxChargingCurrentLimit();
    EXPECT_TRUE(limit.has_value());
    EXPECT_EQ(limit.value(), 10000);  // max charging current from config
}

// Test multiple state transitions
TEST_F(BatteryManagerTest, StateTransitionSequence)
{
    // Start in Idle
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Idle);
    
    // Move to Precharge (voltage between minSafe 9V and precharge 9.6V)
    Measurements measurements{9500, 100};
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Precharge);
    
    // Move to CC (voltage above precharge threshold 9.6V but below max 12.6V)
    measurements.voltage_mV = 11000;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);
    
    // Move to CV (at max voltage 12.6V)
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);
}

// Test updateBatteryProfile applies new limits immediately
TEST_F(BatteryManagerTest, UpdateBatteryProfile_AppliesNewLimitsImmediately)
{
    // Move to CC mode with the default 3S profile (max 12600mV)
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);

    // Swap to 4S profile whose maxVoltage is higher (16800mV)
    batteryManager.updateBatteryProfile(BatteryConfig::LI_ION_4S_DEFAULT);

    // Voltage 12600 is now well below 4S max → must stay in CC, not transition to CV
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);

    // Verify new voltage limit is in effect
    EXPECT_EQ(batteryManager.getMaxVoltageLimit(), std::nullopt);  // not in CV mode
}

// Test updateBatteryProfile mid-charge does not reset state machine
TEST_F(BatteryManagerTest, UpdateBatteryProfile_PreservesCurrentMode)
{
    // Reach CV mode under 3S profile
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);

    // Hot-swap profile — mode must not reset to Idle
    batteryManager.updateBatteryProfile(BatteryConfig::LI_ION_3S_DEFAULT);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);
}

// Test that Fault mode is terminal — no recovery even with good measurements
TEST_F(BatteryManagerTest, FaultModeIsTerminal)
{
    // Below minSafeVoltage (9000mV) → Fault
    Measurements measurements{8000, 1000};
    batteryManager.update(measurements, true);
    ASSERT_EQ(batteryManager.getMode(), BatteryManager::Mode::Fault);
    EXPECT_FALSE(batteryManager.isChargingAllowed());

    // Restore good voltage — Fault must persist (no self-recovery)
    measurements.voltage_mV = 11000;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Fault);
    EXPECT_FALSE(batteryManager.isChargingAllowed());
}

// Test CV -> Done transition when cutoff current is met
TEST_F(BatteryManagerTest, CvToDoneTransition)
{
    // Idle -> CC -> CV
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);  // -> CC
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);  // -> CV (12600 >= maxVoltage 12600)
    ASSERT_EQ(batteryManager.getMode(), BatteryManager::Mode::CV);

    // Current drops below cutoff (100mA) at max voltage -> Done
    measurements.current_mA = 50;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Done);
    EXPECT_FALSE(batteryManager.isChargingAllowed());
}

// Test Done -> CC recharge when voltage drops to rechargeVoltage
TEST_F(BatteryManagerTest, DoneToCcRechargeTransition)
{
    // Reach Done mode
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);  // -> CC
    measurements.voltage_mV = 12600;
    batteryManager.update(measurements, true);  // -> CV
    measurements.current_mA = 50;
    batteryManager.update(measurements, true);  // -> Done
    ASSERT_EQ(batteryManager.getMode(), BatteryManager::Mode::Done);

    // Voltage sags to rechargeVoltage (12400mV) -> CC
    measurements.voltage_mV = 12400;
    measurements.current_mA = 1000;
    batteryManager.update(measurements, true);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);
    EXPECT_TRUE(batteryManager.isChargingAllowed());
}

// Test that Precharge mode returns the prechargeCurrent limit (500mA), not the CC limit
TEST_F(BatteryManagerTest, PrechargeCurrentLimitIs500mA)
{
    // 9500mV is between minSafe (9000) and precharge threshold (9600)
    Measurements measurements{9500, 100};
    batteryManager.update(measurements, true);
    ASSERT_EQ(batteryManager.getMode(), BatteryManager::Mode::Precharge);

    auto limit = batteryManager.getMaxChargingCurrentLimit();
    EXPECT_TRUE(limit.has_value());
    EXPECT_EQ(limit.value(), 500);   // prechargeCurrent_mA, NOT maxChargingCurrent_mA (10000)
}

// Test that charging disabled timer triggers Idle after 60s timeout
TEST_F(BatteryManagerTest, TransitionsToIdleAfterChargingDisabledTimeout)
{
    // Reach CC mode
    Measurements measurements{11000, 1000};
    batteryManager.update(measurements, true);
    ASSERT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);

    // Charging becomes unavailable — triggers the 60s timer
    reset_millis();
    batteryManager.update(measurements, false);
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::CC);  // timer not expired yet

    // Advance past 60s timeout
    advance_millis(60001);
    batteryManager.update(measurements, false);  // timer.update() -> getDuration > 60000 -> Idle
    EXPECT_EQ(batteryManager.getMode(), BatteryManager::Mode::Idle);
    EXPECT_FALSE(batteryManager.isChargingAllowed());
}
