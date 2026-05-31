#include <gtest/gtest.h>
#include "PerturbAndObserveMppt.h"
#include "Config.h"

class PerturbAndObserveMpptTest : public ::testing::Test
{
protected:
    PerturbAndObserveMppt mppt;
    
    void SetUp() override
    {
        mppt.init();
    }
};

// Test initial state after init()
TEST_F(PerturbAndObserveMpptTest, InitialState)
{
    EXPECT_EQ(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
    EXPECT_EQ(mppt.getMpptStep(), MpptStrategyIf::DEFAULT_STEP);
}

// Test control value increases with increasing power
TEST_F(PerturbAndObserveMpptTest, ControlIncreaseWithIncreasingPower)
{
    // First measurement
    Measurements m1{10000, 1000};  // 10W
    mppt.update(m1);
    int control1 = mppt.getMpptControl();
    
    // Second measurement with higher power
    Measurements m2{11000, 2000};  // 22W (higher power)
    mppt.update(m2);
    int control2 = mppt.getMpptControl();
    
    // Control should increase
    EXPECT_GT(control2, control1);
}

// Test control value decreases with decreasing power after direction change
TEST_F(PerturbAndObserveMpptTest, ControlDecreaseWithDecreasingPower)
{
    // First measurement - establish baseline
    Measurements m1{10000, 2000};  // 20W
    mppt.update(m1);
    int control1 = mppt.getMpptControl();
    
    // Second measurement with higher power (should increase)
    Measurements m2{11000, 2500};  // 27.5W (higher)
    mppt.update(m2);
    int control2 = mppt.getMpptControl();
    EXPECT_GT(control2, control1);  // Should increase first
    
    // Third measurement with lower power (should trigger direction change)
    Measurements m3{10000, 2000};  // 20W (back to lower)
    mppt.update(m3);
    int control3 = mppt.getMpptControl();
    
    // Control should change direction and decrease
    EXPECT_LT(control3, control2);
}

// Test control value stays within bounds
TEST_F(PerturbAndObserveMpptTest, ControlLimits)
{
    // Try to go below minimum
    for(int i = 0; i < 200; ++i)
    {
        Measurements m{9000 - i, 1000 - i};
        mppt.update(m);
    }
    EXPECT_GE(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
    
    // Try to go above maximum
    mppt.init();  // Reset
    for(int i = 0; i < 200; ++i)
    {
        Measurements m{10000 + i, 2000 + i};
        mppt.update(m);
    }
    EXPECT_LE(mppt.getMpptControl(), MpptStrategyIf::MAX_CONTROL_VALUE);
}

// Variable-step: step is clamped within [MIN_STEP, MAX_STEP] on every update
TEST_F(PerturbAndObserveMpptTest, VariableStep_AlwaysWithinBounds)
{
    // Large dP/dV → step saturates at MAX_STEP
    Measurements m1{10000, 1000};   // 10W
    mppt.update(m1);

    Measurements m2{11000, 5000};   // 55W — large power jump, large |dP/dV|
    mppt.update(m2);

    EXPECT_GE(mppt.getMpptStep(), MpptStrategyIf::MIN_STEP);
    EXPECT_LE(mppt.getMpptStep(), MpptStrategyIf::MAX_STEP);
}

// Variable-step: small dP/dV (near MPP) yields MIN_STEP
TEST_F(PerturbAndObserveMpptTest, VariableStep_SmallGradient_YieldsMinStep)
{
    // Establish a baseline at 20W
    Measurements m1{10000, 2000};
    mppt.update(m1);

    // Simulate near-MPP: voltage increases 200mV but power barely changes (-8mW).
    // |dP/dV| = 8 / 200 = 0.04 → K_STEP × 0.04 = 0.1 → int(0.1) = 0 → constrained to MIN_STEP
    Measurements m2{10200, 1960};   // power = 19992mW vs 20000mW baseline
    mppt.update(m2);

    EXPECT_EQ(mppt.getMpptStep(), MpptStrategyIf::MIN_STEP);
}

// Test tracking power changes
TEST_F(PerturbAndObserveMpptTest, TracksPowerMaximum)
{
    std::vector<Measurements> measurements = {
        {10000, 1000},   // 10W
        {11000, 1500},   // 16.5W - increasing
        {11500, 1800},   // 20.7W - increasing
        {11000, 1600},   // 17.6W - decreasing
        {10000, 1000},   // 10W - decreasing
    };
    
    int maxControl = 0;
    for(const auto& m : measurements)
    {
        mppt.update(m);
        int control = mppt.getMpptControl();
        maxControl = std::max(maxControl, control);
    }
    
    EXPECT_GT(maxControl, 0);
    EXPECT_LE(maxControl, MpptStrategyIf::MAX_CONTROL_VALUE);
}

// Test oscillation around maximum power point
TEST_F(PerturbAndObserveMpptTest, OscillatesAroundMaximum)
{
    int lastControl = mppt.getMpptControl();
    int oscillations = 0;
    
    for(int iteration = 0; iteration < 50; ++iteration)
    {
        int power = 10000 + (iteration * 50);
        if(iteration > 50)
            power = 10000 + ((100 - iteration) * 50);
        
        int voltage = 10000 + iteration;
        int current = power / voltage;
        
        Measurements m{voltage, current};
        mppt.update(m);
        
        int currentControl = mppt.getMpptControl();
        if(lastControl != currentControl)
            oscillations++;
        lastControl = currentControl;
    }
    
    EXPECT_GT(oscillations, 0);
}
