#include <gtest/gtest.h>
#include "Utility.h"

// Test the Timer class basic functionality
TEST(UtilityTests, TimerBasicOperation)
{
    Timer timer;
    
    // Initially timer should not be active
    EXPECT_FALSE(timer.active());
    EXPECT_EQ(timer.getDuration(), 0);
    
    // After reset, timer should still be inactive and duration 0
    timer.reset();
    EXPECT_FALSE(timer.active());
    EXPECT_EQ(timer.getDuration(), 0);
}

// Test the Timer trigger functionality
TEST(UtilityTests, TimerTrigger)
{
    Timer timer;
    reset_millis();
    advance_millis(1);  // Advance past 0 so timer.start is non-zero
    
    // Trigger the timer
    timer.trigger();
    EXPECT_TRUE(timer.active());    // Should be active after trigger
}

// Test the Timer reset after trigger
TEST(UtilityTests, TimerResetAfterTrigger)
{
    Timer timer;
    reset_millis();
    
    // Verify reset works and timer is inactive
    timer.reset();
    reset_millis();
    EXPECT_FALSE(timer.active());
    EXPECT_EQ(timer.getDuration(), 0);
}

// Test Measurements struct
TEST(UtilityTests, MeasurementsStruct)
{
    Measurements m{};
    EXPECT_EQ(m.voltage_mV, 0);
    EXPECT_EQ(m.current_mA, 0);
    
    Measurements m2{1000, 500};
    EXPECT_EQ(m2.voltage_mV, 1000);
    EXPECT_EQ(m2.current_mA, 500);
}
