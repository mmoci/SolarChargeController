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

// ── Collapse-ceiling tests ────────────────────────────────────────────────────
//
// Panel I-V curve at low irradiance has a hard cliff: when I_SET exceeds Isc,
// Vin collapses abruptly. These tests simulate that cliff by injecting a
// measurement whose voltage drops below the collapse threshold in a single step,
// which is exactly what happens in hardware.
//
// Setup shared across all collapse tests:
//   voc = 40 000 mV  → threshold = 40 000 × 78 % = 31 200 mV
//   normal Vin  = 35 000 mV  (above threshold, panel healthy)
//   collapsed Vin = 26 200 mV  (below threshold, panel stalled)
//
// With these measurements and K_STEP = 0.5:
//   • Update 1 from init: ΔV = 35 000, gradient = 0.25 → step clamped to MIN = 1
//   • Updates 2-N with same measurement: ΔV = 0 → skip → step stays 1
//   → After N updates: control = N  (predictable ceiling arithmetic)

// After a collapse, the ceiling must be set and control must not recover above it.
TEST_F(PerturbAndObserveMpptTest, CollapseGuard_SetsCeilingAndLimitsRecovery)
{
    const int voc_mV = 40000;
    const int threshold_mV = voc_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100; // 31200
    mppt.setOpenCircuitVoltage(voc_mV);

    // Ramp control to 30 with healthy panel measurements
    const Measurements normalM{35000, 500};
    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    ASSERT_EQ(mppt.getMpptControl(), 30);

    // Inject collapse: Vin drops below threshold while previous Vin was above it
    // → ceiling is taken from FIFO minimum: after 30 updates buffer holds {27,28,29} → min = 27
    // (add() is called before control increment so entry N is control value N−1)
    mppt.update({threshold_mV - 5000, 50}); // 26 200 mV

    const int expectedCeiling = 27; // FIFO[3] min after ramp-to-30: entries {27,28,29}

    // Recovery: run enough updates to climb back and reach the ceiling
    for(int i = 0; i < 50; ++i)
        mppt.update(normalM);

    EXPECT_EQ(mppt.getMpptControl(), expectedCeiling);  // Settled at ceiling, not above it
}

// While already in a collapsed state (Vin below threshold two cycles in a row)
// the ceiling must NOT be overwritten downward — only the first entry from
// above threshold should fix the ceiling.
TEST_F(PerturbAndObserveMpptTest, CollapseGuard_CeilingNotOverwrittenDuringOngoingCollapse)
{
    const int voc_mV = 40000;
    const int threshold_mV = voc_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100;
    mppt.setOpenCircuitVoltage(voc_mV);

    const Measurements normalM{35000, 500};
    const Measurements collapseM{threshold_mV - 5000, 50}; // 26 200 mV

    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    ASSERT_EQ(mppt.getMpptControl(), 30);

    // First collapse: ceiling fixed at 10, control drops to 0
    // m_pvMeasurements is now {26 200, 50}
    mppt.update(collapseM);

    // Second collapse while previous Vin is still below threshold
    // → ceiling guard fires but must NOT update the ceiling again
    mppt.update(collapseM);

    // If ceiling was preserved (10): control recovers to 10.
    // If ceiling was overwritten (max(0, 0−20) = 0): control stays stuck at 0.
    for(int i = 0; i < 50; ++i)
        mppt.update(normalM);

    const int expectedCeiling = 27; // FIFO[3] min after ramp-to-30: entries {27,28,29}
    EXPECT_EQ(mppt.getMpptControl(), expectedCeiling);
}

// When irradiance increases (same I_SET but higher Vin), the old ceiling is stale
// and must be cleared so P&O can climb to the new, higher MPP.
TEST_F(PerturbAndObserveMpptTest, CollapseGuard_IrradianceIncreaseClearsCeiling)
{
    const int voc_mV = 40000;
    const int threshold_mV = voc_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100;
    mppt.setOpenCircuitVoltage(voc_mV);

    const Measurements normalM{35000, 500};

    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    mppt.update({threshold_mV - 5000, 50}); // Collapse → ceiling = 10

    // Recover to ceiling and let the baseline Vin (35 000 mV) get recorded
    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    ASSERT_EQ(mppt.getMpptControl(), 27); // At ceiling (FIFO min = 27)

    // Higher irradiance: Vin > baseline (35 000 mV) + IRRADIANCE_INCREASE_VOLTAGE_MARGIN (500 mV)
    // 35 600 mV > 35 500 mV → ceiling should be cleared
    mppt.update({35600, 600});

    // Control is now free to grow past the old ceiling
    for(int i = 0; i < 50; ++i)
        mppt.update({35600, static_cast<int>(600 + i * 10)});

    EXPECT_GT(mppt.getMpptControl(), 27);
}

// ── Proactive ceiling tests ─────────────────────────────────────────────────

// Knee detection: a large ΔVin drop at MIN_STEP while direction is Up (power
// still increasing) signals the steep knee of the I-V curve. The ceiling must
// be set proactively to prevent the next step from crossing into collapse.
//
// Setup:
//   Ramp to control=50 with {35000+i*15, 1500}: each step has ΔV=15mV ≥
//   MIN_DELTA_VOLTAGE_mV so the counter increments every update, reaching 50
//   consecutive min-steps. |dP/dV|=1.5 → step=int(0.75)=1=MIN_STEP throughout.
//   Last ramp measurement: {35735, 1500}.
//   Inject {35535, 1520}: ΔV=-200mV < -KNEE_DELTA_VOLTAGE_THRESHOLD(-150)
//                          ΔP = 35535×1520/1000 − 35735×1500/1000 = +411mW > 0
//                          → direction stays Up, knee fires: ceiling=50
TEST_F(PerturbAndObserveMpptTest, KneeDetection_SetsProactiveCeiling)
{
    for(int i = 0; i < 50; ++i)
        mppt.update({35000 + i * 15, 1500});
    ASSERT_EQ(mppt.getMpptControl(), 50);

    // Inject knee relative to last ramp meas {35735,1500}: ΔV=-200mV, ΔP=+411mW
    mppt.update({35535, 1520});

    // Control must not exceed the knee point even after many further updates
    for(int i = 0; i < 200; ++i)
        mppt.update({35000, 1500});

    EXPECT_LE(mppt.getMpptControl(), 50);
}

// Direction-flip proactive ceiling: the first Down direction flip at MIN_STEP
// sets the ceiling at peakControl (turn-around point), preventing P&O from
// overshooting on the next Up phase.
//
// Setup:
//   Ramp to control=50 with {35200+i*15, 1500}: ΔV=15mV per step, builds
//   m_consecutiveMinSteps to 50. Last ramp measurement: {35935, 1500}.
//   Inject {35735, 1490}: ΔV=-200mV, ΔP=-657mW < 0 → direction flips to Down
//                          gradient=3.29 → step=1=MIN_STEP → ceiling fires at 50
TEST_F(PerturbAndObserveMpptTest, DirectionFlip_SetsProactiveCeiling)
{
    for(int i = 0; i < 50; ++i)
        mppt.update({35200 + i * 15, 1500});
    ASSERT_EQ(mppt.getMpptControl(), 50);

    // Inject flip relative to last ramp meas {35935,1500}: ΔP=-657mW, ΔV=-200mV, step=1
    mppt.update({35735, 1490});
    ASSERT_EQ(mppt.getMpptControl(), 49); // stepped down from peak

    // Control must never exceed the proactive ceiling during full recovery cycle
    // (P&O walks Down to 0, bounces, climbs back to ceiling=50 — needs ~150 updates)
    int maxControl = mppt.getMpptControl();
    for(int i = 0; i < 200; ++i)
    {
        mppt.update({35200, 1500});
        maxControl = std::max(maxControl, mppt.getMpptControl());
    }
    EXPECT_LE(maxControl, 50);
}

// A new Voc reading (e.g. after the 30-min OCV refresh) must also clear the
// ceiling, because a different Voc implies different irradiance conditions.
TEST_F(PerturbAndObserveMpptTest, SetOpenCircuitVoltage_ClearsCeiling)
{
    const int voc_mV = 40000;
    const int threshold_mV = voc_mV * PvArrayConfig::VOLTAGE_COLLAPSE_THRESHOLD_PERCENT / 100;
    mppt.setOpenCircuitVoltage(voc_mV);

    const Measurements normalM{35000, 500};

    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    mppt.update({threshold_mV - 5000, 50}); // Collapse → ceiling = 10

    for(int i = 0; i < 30; ++i)
        mppt.update(normalM);
    ASSERT_EQ(mppt.getMpptControl(), 27); // At ceiling (FIFO min = 27)

    // Simulate OCV refresh with a new (different) Voc value → ceiling must reset
    mppt.setOpenCircuitVoltage(voc_mV + 1000); // Triggers the update branch

    for(int i = 0; i < 50; ++i)
        mppt.update(normalM);

    EXPECT_GT(mppt.getMpptControl(), 27);
}
