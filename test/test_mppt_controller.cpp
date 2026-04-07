#include <gtest/gtest.h>
#include "MpptController.h"

class MpptControllerTest : public ::testing::Test
{
protected:
    MpptController mpptController;
    
    void SetUp() override
    {
        mpptController.init();
    }
};

// Test initial state
TEST_F(MpptControllerTest, InitialState)
{
    EXPECT_EQ(mpptController.getRequestedOutput(), 0);
    EXPECT_EQ(mpptController.getOutputStep(), MpptController::DEFAULT_STEP);
}

// Test control value increases with increasing power
TEST_F(MpptControllerTest, ControlIncreaseWithIncreasingPower)
{
    int initialControl = mpptController.getRequestedOutput();
    
    // First measurement
    Measurements m1{10000, 1000};  // 10W
    mpptController.update(m1);
    int control1 = mpptController.getRequestedOutput();
    
    // Second measurement with higher power
    Measurements m2{11000, 2000};  // 22W (higher power)
    mpptController.update(m2);
    int control2 = mpptController.getRequestedOutput();
    
    // Control should increase
    EXPECT_GT(control2, control1);
}

// Test control value decreases with decreasing power
TEST_F(MpptControllerTest, ControlDecreaseWithDecreasingPower)
{
    // First measurement - establish baseline
    Measurements m1{10000, 2000};  // 20W
    mpptController.update(m1);
    int control1 = mpptController.getRequestedOutput();
    
    // Second measurement with higher power (should increase)
    Measurements m2{11000, 2500};  // 27.5W (higher)
    mpptController.update(m2);
    int control2 = mpptController.getRequestedOutput();
    EXPECT_GT(control2, control1);  // Should increase first
    
    // Third measurement with lower power (should trigger direction change)
    Measurements m3{10000, 2000};  // 20W (back to lower)
    mpptController.update(m3);
    int control3 = mpptController.getRequestedOutput();
    
    // Control should change direction and decrease
    EXPECT_LT(control3, control2);
}

// Test control value stays within bounds
TEST_F(MpptControllerTest, ControlLimits)
{
    // Try to go below minimum
    for(int i = 0; i < 200; ++i)
    {
        Measurements m{9000 - i, 1000 - i};
        mpptController.update(m);
    }
    EXPECT_GE(mpptController.getRequestedOutput(), MpptController::MIN_CONTROL_VALUE);
    
    // Try to go above maximum
    mpptController.init();  // Reset
    for(int i = 0; i < 200; ++i)
    {
        Measurements m{10000 + i, 2000 + i};
        mpptController.update(m);
    }
    EXPECT_LE(mpptController.getRequestedOutput(), MpptController::MAX_CONTROL_VALUE);
}

// Variable-step: step is clamped within [MIN_STEP, MAX_STEP] on every update
TEST_F(MpptControllerTest, VariableStep_AlwaysWithinBounds)
{
    // Large dP/dV → step saturates at MAX_STEP
    Measurements m1{10000, 1000};   // 10W
    mpptController.update(m1);

    Measurements m2{11000, 5000};   // 55W — large power jump, large |dP/dV|
    mpptController.update(m2);

    EXPECT_GE(mpptController.getOutputStep(), MpptController::MIN_STEP);
    EXPECT_LE(mpptController.getOutputStep(), MpptController::MAX_STEP);
}

// Variable-step: small dP/dV (near MPP) yields MIN_STEP
TEST_F(MpptControllerTest, VariableStep_SmallGradient_YieldsMinStep)
{
    // Establish a baseline at 20W
    Measurements m1{10000, 2000};
    mpptController.update(m1);

    // Simulate near-MPP: voltage increases 200mV but power barely changes (-8mW).
    // |dP/dV| = 8 / 200 = 0.04 → K_STEP × 0.04 = 0.1 → int(0.1) = 0 → constrained to MIN_STEP
    Measurements m2{10200, 1960};   // power = 19992mW vs 20000mW baseline
    mpptController.update(m2);

    EXPECT_EQ(mpptController.getOutputStep(), MpptController::MIN_STEP);
}

// Test tracking power changes
TEST_F(MpptControllerTest, TracksPowerMaximum)
{
    // Create a power profile that increases then decreases
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
        mpptController.update(m);
        int control = mpptController.getRequestedOutput();
        maxControl = std::max(maxControl, control);
    }
    
    // Should have increased control during increasing power phase
    EXPECT_GT(maxControl, 0);
    EXPECT_LE(maxControl, MpptController::MAX_CONTROL_VALUE);
}

// Test oscillation around maximum power point
TEST_F(MpptControllerTest, OscillatesAroundMaximum)
{
    int lastControl = mpptController.getRequestedOutput();
    int oscillations = 0;
    
    // Simulate searching for maximum power point
    for(int iteration = 0; iteration < 50; ++iteration)
    {
        // Simulate a power curve with a peak
        int power = 10000 + (iteration * 50);  // Increasing power
        if(iteration > 50)
            power = 10000 + ((100 - iteration) * 50);  // Then decreasing
        
        int voltage = 10000 + iteration;
        int current = power / voltage;
        
        Measurements m{voltage, current};
        mpptController.update(m);
        
        int currentControl = mpptController.getRequestedOutput();
        if(lastControl != currentControl)
            oscillations++;
        lastControl = currentControl;
    }
    
    // Should have some control changes as it searches for MPP
    EXPECT_GT(oscillations, 0);
}
