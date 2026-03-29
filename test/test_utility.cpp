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

// ---------------------------------------------------------------------------
// Timer — correctness at millis() == 0 (regression for start-sentinel bug)
// ---------------------------------------------------------------------------

TEST(UtilityTests, Timer_ActiveAtMillisZero)
{
    Timer timer;
    reset_millis();  // millis() == 0

    timer.trigger();
    EXPECT_TRUE(timer.active());  // must be true even when millis() returns 0
}

TEST(UtilityTests, Timer_DurationTracking)
{
    Timer timer;
    reset_millis();

    timer.trigger();
    advance_millis(250);
    timer.update();

    EXPECT_TRUE(timer.active());
    EXPECT_EQ(timer.getDuration(), 250UL);
}

TEST(UtilityTests, Timer_DurationIsZeroBeforeTrigger)
{
    Timer timer;
    advance_millis(1000);
    timer.update();  // not triggered yet

    EXPECT_FALSE(timer.active());
    EXPECT_EQ(timer.getDuration(), 0UL);
}

TEST(UtilityTests, Timer_ResetClearsDurationAndActive)
{
    Timer timer;
    reset_millis();
    timer.trigger();
    advance_millis(100);
    timer.update();
    ASSERT_TRUE(timer.active());

    timer.reset();

    EXPECT_FALSE(timer.active());
    EXPECT_EQ(timer.getDuration(), 0UL);
}

// ---------------------------------------------------------------------------
// parseIntSafe
// ---------------------------------------------------------------------------

TEST(UtilityTests, ParseIntSafe_ValidPositiveInt_ReturnsTrueAndValue)
{
    int out{};
    EXPECT_TRUE(parseIntSafe("1234", out));
    EXPECT_EQ(out, 1234);
}

TEST(UtilityTests, ParseIntSafe_ValidNegativeInt_ReturnsTrueAndValue)
{
    int out{};
    EXPECT_TRUE(parseIntSafe("-500", out));
    EXPECT_EQ(out, -500);
}

TEST(UtilityTests, ParseIntSafe_Zero_ReturnsTrueAndZero)
{
    int out{};
    EXPECT_TRUE(parseIntSafe("0", out));
    EXPECT_EQ(out, 0);
}

TEST(UtilityTests, ParseIntSafe_EmptyString_ReturnsFalse)
{
    int out{};
    EXPECT_FALSE(parseIntSafe("", out));
}

TEST(UtilityTests, ParseIntSafe_NonNumericString_ReturnsFalse)
{
    int out{};
    EXPECT_FALSE(parseIntSafe("abc", out));
}

TEST(UtilityTests, ParseIntSafe_PartialNumeric_ReturnsTrueForLeadingDigits)
{
    // strtol consumes leading digits; "123abc" → 123
    int out{};
    EXPECT_TRUE(parseIntSafe("123abc", out));
    EXPECT_EQ(out, 123);
}

TEST(UtilityTests, ParseIntSafe_LeadingWhitespace_HandledByStrtol)
{
    // strtol skips leading whitespace
    int out{};
    EXPECT_TRUE(parseIntSafe("  42", out));
    EXPECT_EQ(out, 42);
}
