#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "dash_config_update.h"

void setUp()
{
    isaSpeedChimeSuppressRuntime = kIsaSpeedChimeSuppressDefaultEnabled;
    emergencyVehicleDetectionRuntime = kEmergencyVehicleDetectionDefaultEnabled;
    enhancedAutopilotRuntime = kEnhancedAutopilotDefaultEnabled;
}
void tearDown() {}

// --- setBit ---

void test_setBit_sets_bit0_of_byte0()
{
    CanFrame f = {};
    setBit(f, 0, true);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.data[0]);
}

void test_setBit_sets_bit7_of_byte0()
{
    CanFrame f = {};
    setBit(f, 7, true);
    TEST_ASSERT_EQUAL_HEX8(0x80, f.data[0]);
}

void test_setBit_sets_bit_in_byte5()
{
    CanFrame f = {};
    setBit(f, 46, true); // byte 5, bit 6
    TEST_ASSERT_EQUAL_HEX8(0x40, f.data[5]);
}

void test_setBit_sets_bit_in_byte7()
{
    CanFrame f = {};
    setBit(f, 60, true); // byte 7, bit 4
    TEST_ASSERT_EQUAL_HEX8(0x10, f.data[7]);
}

void test_setBit_clears_bit()
{
    CanFrame f = {};
    f.data[2] = 0xFF;
    setBit(f, 19, false); // byte 2, bit 3
    TEST_ASSERT_EQUAL_HEX8(0xF7, f.data[2]);
}

void test_setBit_does_not_affect_other_bytes()
{
    CanFrame f = {};
    f.data[0] = 0xAA;
    f.data[1] = 0xBB;
    setBit(f, 8, true); // byte 1, bit 0
    TEST_ASSERT_EQUAL_HEX8(0xAA, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, f.data[1]);
}

// --- readMuxID ---

void test_readMuxID_extracts_lower_3_bits()
{
    CanFrame f = {};
    f.data[0] = 0x05;
    TEST_ASSERT_EQUAL_UINT8(5, readMuxID(f));
}

void test_readMuxID_masks_upper_bits()
{
    CanFrame f = {};
    f.data[0] = 0xFA; // binary: 11111010 -> lower 3 = 010 = 2
    TEST_ASSERT_EQUAL_UINT8(2, readMuxID(f));
}

void test_readMuxID_zero()
{
    CanFrame f = {};
    f.data[0] = 0x00;
    TEST_ASSERT_EQUAL_UINT8(0, readMuxID(f));
}

void test_readMuxID_max_value()
{
    CanFrame f = {};
    f.data[0] = 0x07;
    TEST_ASSERT_EQUAL_UINT8(7, readMuxID(f));
}

// --- isFSDSelectedInUI ---

void test_isFSDSelectedInUI_true_when_bit6_set_legacy()
{
    CanFrame f = {};
    f.data[4] = 0x40; // bit 6 set (verified: data[4]>>6)
    TEST_ASSERT_TRUE(isFSDSelectedInUI(f));
}

void test_isFSDSelectedInUI_false_when_all_bits_clear()
{
    CanFrame f = {};
    f.data[4] = 0x00;
    TEST_ASSERT_FALSE(isFSDSelectedInUI(f));
}

void test_isFSDSelectedInUI_ignores_all_bits_except_bit6()
{
    CanFrame f = {};
    f.data[4] = 0x9F; // all bits set except bit 5 and bit 6
    TEST_ASSERT_FALSE(isFSDSelectedInUI(f));
}

void test_isFSDSelectedInUI_true_when_bit6_set()
{
    CanFrame f = {};
    f.data[4] = 0x40; // bit 6 set
    TEST_ASSERT_TRUE(isFSDSelectedInUI(f));
}

void test_isFSDSelectedInUI_true_with_other_bits()
{
    CanFrame f = {};
    f.data[4] = 0xFF;
    TEST_ASSERT_TRUE(isFSDSelectedInUI(f));
}

// --- readGTWAutopilot ---

void test_readGTWAutopilot_extracts_bits_42_to_44()
{
    CanFrame f = {};
    f.data[5] = 0x0C; // 0b011 at bits 42-44
    TEST_ASSERT_EQUAL_UINT8(3, readGTWAutopilot(f));
}

void test_readGTWAutopilot_masks_other_bits()
{
    CanFrame f = {};
    f.data[5] = 0xFF;
    TEST_ASSERT_EQUAL_UINT8(7, readGTWAutopilot(f));
}

// --- DAS autopilot status ---

void test_readDASAutopilotStatus_extracts_lower_nibble()
{
    CanFrame f = {};
    f.data[0] = 0xA5;
    TEST_ASSERT_EQUAL_UINT8(5, readDASAutopilotStatus(f));
}

void test_isDASAutopilotActive_matches_beta15_engaged_only_semantics()
{
    struct Case
    {
        uint8_t state;
        bool active;
    };

    const Case cases[] = {
        {0, false}, // unknown/off
        {1, false}, // standby/off
        {2, false}, // available is NOT engaged; upstream v2.16-beta.15 regression
        {3, true},  // engaged
        {4, true},  // engaged
        {5, true},  // engaged
        {6, true},  // engaged on CN 2026.8.3.6
        {7, false}, // reserved/unknown must fail closed
        {8, false}, // handover/warning must fail closed
        {9, false}, // fault/aborted must fail closed
        {15, false},
    };

    for (const auto &c : cases)
    {
        TEST_ASSERT_EQUAL_MESSAGE(c.active, isDASAutopilotActive(c.state), "DAS AP active state mismatch");
    }
}

// --- Gear state ---

void test_readVehicleGear_extracts_dif_gear_bits()
{
    CanFrame f = {};
    f.data[7] = static_cast<uint8_t>(4U << 3);
    TEST_ASSERT_EQUAL_UINT8(4, readVehicleGear(f));
}

void test_isVehicleParked_true_for_park()
{
    TEST_ASSERT_TRUE(isVehicleParked(1));
}

void test_isVehicleParked_false_for_drive()
{
    TEST_ASSERT_FALSE(isVehicleParked(4));
}

void test_isVehicleParked_false_for_sna()
{
    // Live SNA must fail closed. It must not open AP Injection Gate as Park.
    TEST_ASSERT_FALSE(isVehicleParked(7));
}

void test_isVehicleParked_false_for_invalid()
{
    // Live INVALID must fail closed. It must not open AP Injection Gate as Park.
    TEST_ASSERT_FALSE(isVehicleParked(0));
}

void test_isVehicleParked_false_for_reverse_neutral()
{
    TEST_ASSERT_FALSE(isVehicleParked(2));
    TEST_ASSERT_FALSE(isVehicleParked(3));
}

// --- setSpeedProfileV12V13 ---

void test_setSpeedProfileV12V13_sets_profile_0()
{
    CanFrame f = {};
    f.data[6] = 0xFF;
    setSpeedProfileV12V13(f, 0);
    TEST_ASSERT_EQUAL_HEX8(0xF9, f.data[6]); // bits 1-2 cleared
}

void test_setSpeedProfileV12V13_sets_profile_1()
{
    CanFrame f = {};
    f.data[6] = 0x00;
    setSpeedProfileV12V13(f, 1);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.data[6]);
}

void test_setSpeedProfileV12V13_sets_profile_2()
{
    CanFrame f = {};
    f.data[6] = 0x00;
    setSpeedProfileV12V13(f, 2);
    TEST_ASSERT_EQUAL_HEX8(0x04, f.data[6]);
}

void test_setSpeedProfileV12V13_preserves_other_bits()
{
    CanFrame f = {};
    f.data[6] = 0xF9; // bits 1-2 clear, rest set
    setSpeedProfileV12V13(f, 1);
    TEST_ASSERT_EQUAL_HEX8(0xFB, f.data[6]);
}

void test_computeVehicleChecksum_sums_payload_and_frame_id()
{
    CanFrame f = {.id = 1021, .dlc = 8};
    f.data[0] = 0xFD;
    f.data[1] = 0x10;
    f.data[2] = 0x20;
    f.data[3] = 0x04;
    f.data[4] = 0x00;
    f.data[5] = 0x00;
    f.data[6] = 0xA0;
    f.data[7] = 0x00;
    TEST_ASSERT_EQUAL_HEX8(0xD1, computeVehicleChecksum(f));
}

// --- Runtime defaults ---

void test_ui_bit_clear_reads_frame_as_false()
{
    CanFrame f = {};
    f.data[4] = 0x00;
    TEST_ASSERT_FALSE(isFSDSelectedInUI(f));
}

void test_ui_bit5_is_not_detected()
{
    CanFrame f = {};
    f.data[4] = 0x20; // bit 5 only — NOT the FSD bit (which is bit 6)
    TEST_ASSERT_FALSE(isFSDSelectedInUI(f));
}

void test_ui_bit6_still_reads_real_bit()
{
    CanFrame f = {};
    f.data[4] = 0x40;
    TEST_ASSERT_TRUE(isFSDSelectedInUI(f));
}

void test_runtime_defaults_start_disabled()
{
    TEST_ASSERT_EQUAL(kIsaSpeedChimeSuppressDefaultEnabled, isaSpeedChimeSuppressRuntime);
    TEST_ASSERT_EQUAL(kEmergencyVehicleDetectionDefaultEnabled, emergencyVehicleDetectionRuntime);
    TEST_ASSERT_EQUAL(kEnhancedAutopilotDefaultEnabled, enhancedAutopilotRuntime);
}

// --- dashSoftEngageRelease (Soft Engage angle gate) ---

void test_softEngageRelease_disabled_bypasses()
{
    // toggle OFF → always release (V1.0.3 behaviour) regardless of angle
    TEST_ASSERT_TRUE(dashSoftEngageRelease(false, false, true, 0, 100,
                                           true, false, 50));
}

void test_softEngageRelease_already_sent_latches()
{
    // already activated this episode → ignore angle (mid-corner safe)
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, true, true, 0, 300,
                                           true, false, 50));
}

void test_softEngageRelease_not_settle_defers_to_settle_gate()
{
    // settle gate not yet passed → not soft-engage's job → release=true
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 300,
                                           false, false, 50));
}

void test_softEngageRelease_centred_releases()
{
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 0,
                                           true, false, 50));
}

void test_softEngageRelease_off_centre_holds()
{
    // |angle|=100 > 50, no timeout → HOLD (the core behaviour)
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, true, 0, 100,
                                            true, false, 50));
}

void test_softEngageRelease_off_centre_timeout_releases()
{
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 100,
                                           true, true, 50));
}

void test_softEngageRelease_unseen_angle_holds()
{
    // steerSeen=false (no 0x129 yet) → hold until timeout
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, false, 0, 0,
                                            true, false, 50));
}

void test_softEngageRelease_invalid_validity_holds()
{
    // steerValidity != 0 → signal invalid → hold until timeout
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, true, 1, 0,
                                            true, false, 50));
}

void test_softEngageRelease_threshold_boundary_is_inclusive()
{
    // |angle| == threshold (50) → centred (<= inclusive)
    TEST_ASSERT_TRUE(dashSoftEngageRelease(true, false, true, 0, 50,
                                           true, false, 50));
}

void test_softEngageRelease_negative_angle_holds()
{
    // Right-turn sign convention: steerAngleX10 is NEGATIVE (e.g. -100).
    // Guards the abs() — without it, -100 <= 50 would be TRUE and defeat the
    // gate. |−100| = 100 > 50, no timeout → HOLD.
    TEST_ASSERT_FALSE(dashSoftEngageRelease(true, false, true, 0, -100,
                                            true, false, 50));
}

void test_strict_bool_parser_accepts_documented_values_case_insensitively()
{
    struct Case
    {
        const char *raw;
        bool expected;
    };
    const Case cases[] = {
        {"0", false},
        {"1", true},
        {"false", false},
        {"TRUE", true},
        {"Off", false},
        {"oN", true},
        {"NO", false},
        {"Yes", true},
    };

    for (const auto &c : cases)
    {
        bool parsed = !c.expected;
        TEST_ASSERT_TRUE_MESSAGE(dashParseStrictBool(c.raw, parsed), c.raw);
        TEST_ASSERT_EQUAL_MESSAGE(c.expected, parsed, c.raw);
    }
}

void test_strict_bool_parser_rejects_malformed_and_decorated_values()
{
    const char *invalid[] = {
        "",
        "2",
        "-1",
        "enabled",
        "truthy",
        "truex",
        " true",
        "true ",
        "yes!",
        "0x",
    };

    for (const char *raw : invalid)
    {
        bool parsed = true;
        TEST_ASSERT_FALSE_MESSAGE(dashParseStrictBool(raw, parsed), raw);
        TEST_ASSERT_TRUE_MESSAGE(parsed, raw);
    }

    bool parsed = false;
    TEST_ASSERT_FALSE(dashParseStrictBool(nullptr, parsed));
    TEST_ASSERT_FALSE(parsed);
}

void test_persisted_bool_update_invalid_input_never_calls_persistence()
{
    int calls = 0;
    auto update = dashPreparePersistedBoolUpdate(
        "truex", true,
        [&calls](bool)
        {
            calls++;
            return true;
        });

    TEST_ASSERT_FALSE(update.valid);
    TEST_ASSERT_FALSE(update.persisted);
    TEST_ASSERT_TRUE(update.value);
    TEST_ASSERT_EQUAL_INT(0, calls);
}

void test_persisted_bool_update_failure_preserves_runtime_value()
{
    int calls = 0;
    bool attempted = false;
    auto update = dashPreparePersistedBoolUpdate(
        "yes", false,
        [&calls, &attempted](bool value)
        {
            calls++;
            attempted = value;
            return false;
        });

    TEST_ASSERT_TRUE(update.valid);
    TEST_ASSERT_FALSE(update.persisted);
    TEST_ASSERT_FALSE(update.value);
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_TRUE(attempted);
}

void test_persisted_bool_update_success_returns_new_value_after_write()
{
    int calls = 0;
    bool persistedValue = true;
    auto update = dashPreparePersistedBoolUpdate(
        "off", true,
        [&calls, &persistedValue](bool value)
        {
            calls++;
            persistedValue = value;
            return true;
        });

    TEST_ASSERT_TRUE(update.valid);
    TEST_ASSERT_TRUE(update.persisted);
    TEST_ASSERT_FALSE(update.value);
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_FALSE(persistedValue);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_setBit_sets_bit0_of_byte0);
    RUN_TEST(test_setBit_sets_bit7_of_byte0);
    RUN_TEST(test_setBit_sets_bit_in_byte5);
    RUN_TEST(test_setBit_sets_bit_in_byte7);
    RUN_TEST(test_setBit_clears_bit);
    RUN_TEST(test_setBit_does_not_affect_other_bytes);

    RUN_TEST(test_readMuxID_extracts_lower_3_bits);
    RUN_TEST(test_readMuxID_masks_upper_bits);
    RUN_TEST(test_readMuxID_zero);
    RUN_TEST(test_readMuxID_max_value);

    RUN_TEST(test_isFSDSelectedInUI_true_when_bit6_set_legacy);
    RUN_TEST(test_isFSDSelectedInUI_false_when_all_bits_clear);
    RUN_TEST(test_isFSDSelectedInUI_ignores_all_bits_except_bit6);
    RUN_TEST(test_isFSDSelectedInUI_true_when_bit6_set);
    RUN_TEST(test_isFSDSelectedInUI_true_with_other_bits);
    RUN_TEST(test_readGTWAutopilot_extracts_bits_42_to_44);
    RUN_TEST(test_readGTWAutopilot_masks_other_bits);
    RUN_TEST(test_readDASAutopilotStatus_extracts_lower_nibble);
    RUN_TEST(test_isDASAutopilotActive_matches_beta15_engaged_only_semantics);
    RUN_TEST(test_readVehicleGear_extracts_dif_gear_bits);
    RUN_TEST(test_isVehicleParked_true_for_park);
    RUN_TEST(test_isVehicleParked_false_for_drive);
    RUN_TEST(test_isVehicleParked_false_for_sna);
    RUN_TEST(test_isVehicleParked_false_for_invalid);
    RUN_TEST(test_isVehicleParked_false_for_reverse_neutral);

    RUN_TEST(test_setSpeedProfileV12V13_sets_profile_0);
    RUN_TEST(test_setSpeedProfileV12V13_sets_profile_1);
    RUN_TEST(test_setSpeedProfileV12V13_sets_profile_2);
    RUN_TEST(test_setSpeedProfileV12V13_preserves_other_bits);
    RUN_TEST(test_computeVehicleChecksum_sums_payload_and_frame_id);

    RUN_TEST(test_ui_bit_clear_reads_frame_as_false);
    RUN_TEST(test_ui_bit5_is_not_detected);
    RUN_TEST(test_ui_bit6_still_reads_real_bit);
    RUN_TEST(test_runtime_defaults_start_disabled);

    RUN_TEST(test_softEngageRelease_disabled_bypasses);
    RUN_TEST(test_softEngageRelease_already_sent_latches);
    RUN_TEST(test_softEngageRelease_not_settle_defers_to_settle_gate);
    RUN_TEST(test_softEngageRelease_centred_releases);
    RUN_TEST(test_softEngageRelease_off_centre_holds);
    RUN_TEST(test_softEngageRelease_off_centre_timeout_releases);
    RUN_TEST(test_softEngageRelease_unseen_angle_holds);
    RUN_TEST(test_softEngageRelease_invalid_validity_holds);
    RUN_TEST(test_softEngageRelease_threshold_boundary_is_inclusive);
    RUN_TEST(test_softEngageRelease_negative_angle_holds);

    RUN_TEST(test_strict_bool_parser_accepts_documented_values_case_insensitively);
    RUN_TEST(test_strict_bool_parser_rejects_malformed_and_decorated_values);
    RUN_TEST(test_persisted_bool_update_invalid_input_never_calls_persistence);
    RUN_TEST(test_persisted_bool_update_failure_preserves_runtime_value);
    RUN_TEST(test_persisted_bool_update_success_returns_new_value_after_write);

    return UNITY_END();
}
