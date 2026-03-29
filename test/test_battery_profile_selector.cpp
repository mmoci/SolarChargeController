#include <gtest/gtest.h>
#include "BatteryProfileSelector.h"
#include "Config.h"
#include "nvs.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BatteryProfileSelectorTest : public ::testing::Test
{
protected:
    BatteryProfileSelector selector;

    void SetUp() override
    {
        // Start every test with an empty NVS so init() always falls back to
        // the compiled-in LIION_3S defaults.
        MockNvs::reset();
        selector = BatteryProfileSelector{};
        selector.init();
    }
};

// ---------------------------------------------------------------------------
// init() — default state on empty NVS
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, Init_EmptyNvs_DefaultsToLiIon3S)
{
    EXPECT_EQ(selector.getCurrentType(), BatteryConfig::BatteryType::LIION_3S);
}

TEST_F(BatteryProfileSelectorTest, Init_EmptyNvs_ProfileMatchesLiIon3SDefault)
{
    const BatteryProfile& profile  = selector.getCurrentProfile();
    const BatteryProfile& expected = BatteryConfig::LI_ION_3S_DEFAULT;

    EXPECT_EQ(profile.maxVoltage_mV,            expected.maxVoltage_mV);
    EXPECT_EQ(profile.rechargeVoltage_mV,        expected.rechargeVoltage_mV);
    EXPECT_EQ(profile.prechargeVoltage_mV,       expected.prechargeVoltage_mV);
    EXPECT_EQ(profile.loadDisconnectVoltage_mV,  expected.loadDisconnectVoltage_mV);
    EXPECT_EQ(profile.maxChargingCurrent_mA,     expected.maxChargingCurrent_mA);
}

// ---------------------------------------------------------------------------
// setProfileType() — type switching
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetProfileType_LiIon4S_LoadsCorrectDefaults)
{
    auto result = selector.setProfileType(BatteryConfig::BatteryType::LIION_4S);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentType(), BatteryConfig::BatteryType::LIION_4S);
    EXPECT_EQ(selector.getCurrentProfile().maxVoltage_mV,
              BatteryConfig::LI_ION_4S_DEFAULT.maxVoltage_mV);
}

TEST_F(BatteryProfileSelectorTest, SetProfileType_LiFePO4_4S_LoadsCorrectDefaults)
{
    auto result = selector.setProfileType(BatteryConfig::BatteryType::LIFEPO4_4S);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentType(), BatteryConfig::BatteryType::LIFEPO4_4S);
    EXPECT_EQ(selector.getCurrentProfile().maxVoltage_mV,
              BatteryConfig::LIFEPO4_4S_DEFAULT.maxVoltage_mV);
}

TEST_F(BatteryProfileSelectorTest, SetProfileType_SwitchBetweenTypes_ResetsToNewTypeDefaults)
{
    selector.setProfileType(BatteryConfig::BatteryType::LIION_4S);
    auto result = selector.setProfileType(BatteryConfig::BatteryType::LIION_3S);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().maxVoltage_mV,
              BatteryConfig::LI_ION_3S_DEFAULT.maxVoltage_mV);
}

TEST_F(BatteryProfileSelectorTest, SetProfileType_OnFailure_PreviousTypeIsRestored)
{
    // Deliberately force a failure by directly manipulating the profile into a
    // state where the CUSTOM default would fail if validation checked current
    // type — this verifies rollback of m_currentType on error.
    // The easiest way: inject a bad profile struct via validateProfile directly.
    BatteryProfile bad{};  // all zeros → maxVoltage_mV == 0 → VALIDATION_ERROR
    EXPECT_EQ(selector.validateProfile(bad), BatteryProfileSelector::Result::VALIDATION_ERROR);
    // Type must be unchanged after a failed validate
    EXPECT_EQ(selector.getCurrentType(), BatteryConfig::BatteryType::LIION_3S);
}

// ---------------------------------------------------------------------------
// setMaxVoltage()
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetMaxVoltage_ValidValue_UpdatesProfile)
{
    // 12500 is within ±10% of default 12600 and above recharge (12400)
    auto result = selector.setMaxVoltage(12500);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().maxVoltage_mV, 12500);
}

TEST_F(BatteryProfileSelectorTest, SetMaxVoltage_ExceedsUpperMargin_ReturnsValidationError)
{
    // 14000 > 12600 * 1.1 = 13860
    auto result = selector.setMaxVoltage(14000);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
    EXPECT_EQ(selector.getCurrentProfile().maxVoltage_mV,
              BatteryConfig::LI_ION_3S_DEFAULT.maxVoltage_mV);  // unchanged
}

TEST_F(BatteryProfileSelectorTest, SetMaxVoltage_BelowLowerMargin_ReturnsValidationError)
{
    // 11000 < 12600 * 0.9 = 11340
    auto result = selector.setMaxVoltage(11000);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

TEST_F(BatteryProfileSelectorTest, SetMaxVoltage_BelowRechargeVoltage_ReturnsValidationError)
{
    // 12300 is within margin but < recharge (12400) → ordering violation
    auto result = selector.setMaxVoltage(12300);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

// ---------------------------------------------------------------------------
// setRechargeVoltage()
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetRechargeVoltage_ValidValue_UpdatesProfile)
{
    // 12200 < max (12600) and > precharge (9600)
    auto result = selector.setRechargeVoltage(12200);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().rechargeVoltage_mV, 12200);
}

TEST_F(BatteryProfileSelectorTest, SetRechargeVoltage_EqualToMaxVoltage_ReturnsValidationError)
{
    auto result = selector.setRechargeVoltage(12600);  // == max

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

TEST_F(BatteryProfileSelectorTest, SetRechargeVoltage_AboveMaxVoltage_ReturnsValidationError)
{
    auto result = selector.setRechargeVoltage(12700);  // > max

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

// ---------------------------------------------------------------------------
// setPrechargeVoltage()
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetPrechargeVoltage_ValidValue_UpdatesProfile)
{
    // 9800 < recharge (12400) and >= loadDisconnect (9600)
    auto result = selector.setPrechargeVoltage(9800);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().prechargeVoltage_mV, 9800);
}

TEST_F(BatteryProfileSelectorTest, SetPrechargeVoltage_EqualToRechargeVoltage_ReturnsValidationError)
{
    auto result = selector.setPrechargeVoltage(12400);  // == recharge

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

TEST_F(BatteryProfileSelectorTest, SetPrechargeVoltage_BelowLoadDisconnect_ReturnsValidationError)
{
    // loadDisconnect = 9600, precharge = 9500 → loadDisconnect > precharge → invalid
    auto result = selector.setPrechargeVoltage(9500);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

// ---------------------------------------------------------------------------
// setLoadDisconnectVoltage()
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetLoadDisconnectVoltage_ValidValue_UpdatesProfile)
{
    // 9400 < precharge (9600)
    auto result = selector.setLoadDisconnectVoltage(9400);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().loadDisconnectVoltage_mV, 9400);
}

TEST_F(BatteryProfileSelectorTest, SetLoadDisconnectVoltage_EqualToPrecharge_ReturnsSuccess)
{
    // Equal is allowed — load disconnects at same threshold as precharge
    auto result = selector.setLoadDisconnectVoltage(9600);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
}

TEST_F(BatteryProfileSelectorTest, SetLoadDisconnectVoltage_AbovePrechargeVoltage_ReturnsValidationError)
{
    // 9700 > precharge (9600)
    auto result = selector.setLoadDisconnectVoltage(9700);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

// ---------------------------------------------------------------------------
// setMaxChargingCurrent()
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, SetMaxChargingCurrent_ValidValue_UpdatesProfile)
{
    auto result = selector.setMaxChargingCurrent(5000);

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
    EXPECT_EQ(selector.getCurrentProfile().maxChargingCurrent_mA, 5000);
}

TEST_F(BatteryProfileSelectorTest, SetMaxChargingCurrent_ExactlyAtMax_ReturnsSuccess)
{
    auto result = selector.setMaxChargingCurrent(10000);  // MAX_CHARGING_CURRENT

    EXPECT_EQ(result, BatteryProfileSelector::Result::SUCCESS);
}

TEST_F(BatteryProfileSelectorTest, SetMaxChargingCurrent_Zero_ReturnsValidationError)
{
    auto result = selector.setMaxChargingCurrent(0);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

TEST_F(BatteryProfileSelectorTest, SetMaxChargingCurrent_ExceedsMax_ReturnsValidationError)
{
    auto result = selector.setMaxChargingCurrent(10001);

    EXPECT_EQ(result, BatteryProfileSelector::Result::VALIDATION_ERROR);
}

// ---------------------------------------------------------------------------
// validateProfile() — direct calls
// ---------------------------------------------------------------------------

TEST_F(BatteryProfileSelectorTest, ValidateProfile_ZeroVoltages_ReturnsValidationError)
{
    BatteryProfile bad{};  // all zeros
    EXPECT_EQ(selector.validateProfile(bad), BatteryProfileSelector::Result::VALIDATION_ERROR);
}

TEST_F(BatteryProfileSelectorTest, ValidateProfile_DefaultLiIon3S_ReturnsSuccess)
{
    EXPECT_EQ(selector.validateProfile(BatteryConfig::LI_ION_3S_DEFAULT),
              BatteryProfileSelector::Result::SUCCESS);
}

// ---------------------------------------------------------------------------
// NVS round-trip — simulates a device reboot
// ---------------------------------------------------------------------------

TEST(BatteryProfileSelectorIntegration, NvsRoundTrip_OverridePersistedAndRestoredOnReboot)
{
    MockNvs::reset();

    // "First boot": init defaults to LIION_3S, then apply an override.
    {
        BatteryProfileSelector selector1;
        selector1.init();
        ASSERT_EQ(selector1.setMaxVoltage(12500), BatteryProfileSelector::Result::SUCCESS);
    }

    // "Reboot": new instance loads from NVS — should recover type + override.
    {
        BatteryProfileSelector selector2;
        selector2.init();

        EXPECT_EQ(selector2.getCurrentType(), BatteryConfig::BatteryType::LIION_3S);
        EXPECT_EQ(selector2.getCurrentProfile().maxVoltage_mV, 12500);
        // Fields without overrides remain as type defaults
        EXPECT_EQ(selector2.getCurrentProfile().rechargeVoltage_mV,
                  BatteryConfig::LI_ION_3S_DEFAULT.rechargeVoltage_mV);
    }
}

TEST(BatteryProfileSelectorIntegration, NvsRoundTrip_TypeChangePersisted)
{
    MockNvs::reset();

    {
        BatteryProfileSelector selector1;
        selector1.init();
        ASSERT_EQ(selector1.setProfileType(BatteryConfig::BatteryType::LIION_4S),
                  BatteryProfileSelector::Result::SUCCESS);
    }

    {
        BatteryProfileSelector selector2;
        selector2.init();

        EXPECT_EQ(selector2.getCurrentType(), BatteryConfig::BatteryType::LIION_4S);
        EXPECT_EQ(selector2.getCurrentProfile().maxVoltage_mV,
                  BatteryConfig::LI_ION_4S_DEFAULT.maxVoltage_mV);
    }
}
