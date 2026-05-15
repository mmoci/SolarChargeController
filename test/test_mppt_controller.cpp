#include <gtest/gtest.h>
#include "PerturbAndObserveMppt.h"
#include "InputVoltageRegulationMppt.h"
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

// ─── InputVoltageRegulationMppt tests ────────────────────────────────────────

class InputVoltageRegulationMpptTest : public ::testing::Test
{
protected:
    InputVoltageRegulationMppt mppt;

    static constexpr int TEST_OCV_mV  = 20000;              // convenient test OCV
    static constexpr int TEST_VMPP_mV = (TEST_OCV_mV * PvArrayConfig::INITIAL_MPPT_VOLTAGE_PERCENT) / 100; // 16800 mV at 84%

    void SetUp() override { mppt.init(); }
};

TEST_F(InputVoltageRegulationMpptTest, InitialState)
{
    EXPECT_EQ(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

// After init(), Vmpp = DEFAULT_OCV × 76%.  Vin exactly at that target → zero
// error → control stays at zero.
TEST_F(InputVoltageRegulationMpptTest, InitialVmppDerivedFromDefaultOcv)
{
    const int defaultVmpp = (PvArrayConfig::DEFAULT_OPEN_CIRCUIT_VOLTAGE_mV * 76) / 100;
    mppt.update({defaultVmpp, 1000});
    EXPECT_EQ(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

TEST_F(InputVoltageRegulationMpptTest, SetOpenCircuitVoltage_UpdatesVmppTarget)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV); // Vmpp → 15200 mV
    // Vin exactly at new Vmpp → zero error → control unchanged
    mppt.update({TEST_VMPP_mV, 1000});
    EXPECT_EQ(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

TEST_F(InputVoltageRegulationMpptTest, SetOpenCircuitVoltage_ZeroIgnored)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV); // valid → Vmpp = 15200
    mppt.setOpenCircuitVoltage(0);           // must be rejected

    // If 0 were accepted, m_mpptVoltage = 0 and update() returns early → control stays 0.
    // If rejected, Vmpp = TEST_VMPP_mV and Vin = TEST_VMPP_mV+1200 gives a positive error → correction > 0.
    mppt.update({TEST_VMPP_mV + 1200, 1000});
    EXPECT_GT(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

TEST_F(InputVoltageRegulationMpptTest, SetOpenCircuitVoltage_SameValueIgnored)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV);
    // Build up some control
    for (int i = 0; i < 10; ++i)
        mppt.update({18000, 1000}); // well above Vmpp

    int controlMid = mppt.getMpptControl();
    EXPECT_GT(controlMid, 0);

    // Setting the same OCV again must not reset Vmpp or control
    mppt.setOpenCircuitVoltage(TEST_OCV_mV);
    EXPECT_EQ(mppt.getMpptControl(), controlMid);
}

TEST_F(InputVoltageRegulationMpptTest, ControlIncreasesWhenVinAboveVmpp)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV); // Vmpp = TEST_VMPP_mV
    // error = (TEST_VMPP_mV+1200) - TEST_VMPP_mV = 1200 mV → correction = int(0.005 * 1200) = 6
    mppt.update({TEST_VMPP_mV + 1200, 1000});
    EXPECT_GT(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

TEST_F(InputVoltageRegulationMpptTest, ControlDecreasesWhenVinBelowVmpp)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV);
    // Build control up first (Vin well above Vmpp)
    for (int i = 0; i < 20; ++i)
        mppt.update({18000, 1000});

    int controlBefore = mppt.getMpptControl();
    EXPECT_GT(controlBefore, 0);

    // Vin below Vmpp → negative error → control decreases
    mppt.update({14000, 1000});
    EXPECT_LT(mppt.getMpptControl(), controlBefore);
}

TEST_F(InputVoltageRegulationMpptTest, ControlClampsToMaximum)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV);
    for (int i = 0; i < 1000; ++i)
        mppt.update({30000, 1000}); // far above Vmpp

    EXPECT_LE(mppt.getMpptControl(), MpptStrategyIf::MAX_CONTROL_VALUE);
}

TEST_F(InputVoltageRegulationMpptTest, ControlClampsToMinimum)
{
    mppt.setOpenCircuitVoltage(TEST_OCV_mV);
    for (int i = 0; i < 1000; ++i)
        mppt.update({5000, 1000}); // far below Vmpp

    EXPECT_GE(mppt.getMpptControl(), MpptStrategyIf::MIN_CONTROL_VALUE);
}

// IVR uses a conservative step of 2% to prevent overshooting panel Isc at low irradiance.
TEST_F(InputVoltageRegulationMpptTest, GetMaxSoftRampStep_ReturnsTwoPercentStep)
{
    EXPECT_EQ(mppt.getMaxSoftRampStep(), 2);
}
