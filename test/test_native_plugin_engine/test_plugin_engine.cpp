#include <unity.h>
#include "dash_plugin_engine.h"
#include "drivers/mock_driver.h"

static MockDriver driver;

static CanFrame frame2047(uint8_t mux = 3)
{
    CanFrame f{};
    f.id = 2047;
    f.bus = CAN_BUS_DEFAULT;
    f.dlc = 8;
    f.data[0] = mux;
    return f;
}

static CanFrame frame921()
{
    CanFrame f{};
    f.id = 921;
    f.bus = CAN_BUS_DEFAULT;
    f.dlc = 8;
    return f;
}

static DashPluginContext allowedContext()
{
    DashPluginContext ctx{};
    ctx.canActive = true;
    ctx.otaAllowed = true;
    ctx.apGateAllowed = true;
    ctx.fsdMasterEnabled = true;
    ctx.defaultBus = CAN_BUS_DEFAULT;
    return ctx;
}

static uint8_t pluginTestChecksumByte(const CanFrame &frame)
{
    uint16_t sum = static_cast<uint16_t>(frame.id & 0xFF) + static_cast<uint16_t>((frame.id >> 8) & 0xFF);
    for (uint8_t i = 0; i < 7; ++i)
        sum += frame.data[i];
    return static_cast<uint8_t>(sum & 0xFF);
}

void setUp()
{
    driver.reset();
}

void tearDown() {}

void test_valid_official_plugin_installs_disabled_by_default()
{
    DashPluginEngine engine;
    const char *json = R"json({
      "name":"My Plugin",
      "version":"1.0",
      "author":"Tester",
      "rules":[{"id":921,"mux":-1,"ops":[{"type":"set_bit","bit":13,"val":1}],"send":true}]
    })json";

    DashPluginResult result = engine.installJson(json, false);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(1, engine.pluginCount());
    TEST_ASSERT_FALSE(engine.pluginAt(0).enabled);
    TEST_ASSERT_EQUAL_STRING("My Plugin", engine.pluginAt(0).name.c_str());
    TEST_ASSERT_EQUAL(1, engine.pluginAt(0).priority);
}

void test_invalid_json_is_rejected_without_installing()
{
    DashPluginEngine engine;
    DashPluginResult result = engine.installJson("{not-json", false);

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL(0, engine.pluginCount());
    TEST_ASSERT_TRUE(result.message.length() > 0);
}

void test_name_at_limit_is_accepted()
{
    // Names up to kDashPluginMaxNameLen (63) are accepted — descriptive names
    // like "Bypass TLSSC + FSD HW4 v2" must install without being mistaken for
    // an oversized filename.
    DashPluginEngine engine;
    char name[64];
    memset(name, 'a', 63);
    name[63] = '\0';
    char json[256];
    snprintf(json, sizeof(json),
             R"json({"name":"%s","rules":[{"id":921,"ops":[{"type":"checksum"}]}]})json", name);

    DashPluginResult result = engine.installJson(json, false);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(1, engine.pluginCount());
}

void test_name_longer_than_max_chars_is_rejected()
{
    // Names exceeding kDashPluginMaxNameLen (63) are rejected with a clear
    // message that names the JSON "name" field (not the upload filename).
    DashPluginEngine engine;
    char name[66];
    memset(name, 'a', 65);
    name[65] = '\0';
    char json[256];
    snprintf(json, sizeof(json),
             R"json({"name":"%s","rules":[{"id":921,"ops":[{"type":"checksum"}]}]})json", name);

    DashPluginResult result = engine.installJson(json, false);

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL(0, engine.pluginCount());
    TEST_ASSERT_TRUE(result.message.find("\"name\"") != std::string::npos);
}

void test_installing_same_name_replaces_existing_plugin_disabled()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"ReplaceMe","rules":[{"id":921,"ops":[{"type":"set_bit","bit":1}]}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("ReplaceMe", true).ok);

    DashPluginResult result = engine.installJson(R"json({"name":"ReplaceMe","version":"2.0","rules":[{"id":2047,"ops":[{"type":"checksum"}]}]})json", false);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL(1, engine.pluginCount());
    TEST_ASSERT_EQUAL_STRING("2.0", engine.pluginAt(0).version.c_str());
    TEST_ASSERT_FALSE(engine.pluginAt(0).enabled);
}

void test_disabled_plugin_does_not_send()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Disabled","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}],"send":true}]})json", false).ok);

    CanFrame f = frame921();
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_FALSE(applied.sent);
    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_enabled_plugin_applies_set_bit_and_sends_once()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Enabled","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Enabled", true).ok);

    CanFrame f = frame921();
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_TRUE((driver.sent[0].data[1] & 0x20) != 0);
}

void test_safety_context_blocks_plugin_execution()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Safe","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Safe", true).ok);

    DashPluginContext ctx = allowedContext();
    ctx.apGateAllowed = false;
    CanFrame f = frame921();
    DashPluginApplyResult applied = engine.applyToFrame(f, ctx, driver);

    TEST_ASSERT_FALSE(applied.sent);
    TEST_ASSERT_EQUAL(0, driver.sent.size());
    TEST_ASSERT_EQUAL_STRING("ap_gate", applied.blockedBy.c_str());
}

void test_priority_prevents_lower_plugin_from_overwriting_same_bit()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"High","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13,"val":1}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Low","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13,"val":0}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("High", true).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Low", true).ok);
    TEST_ASSERT_TRUE(engine.setPriority("High", 1).ok);
    TEST_ASSERT_TRUE(engine.setPriority("Low", 2).ok);

    CanFrame f = frame921();
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_TRUE((driver.sent[0].data[1] & 0x20) != 0);
    TEST_ASSERT_TRUE(engine.statusJson().indexOf("Priority overlap") >= 0);
}

void test_gtw2047_replay_repeats_only_modified_2047_frames()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"GTW","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":170},{"type":"checksum"}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("GTW", true).ok);
    TEST_ASSERT_TRUE(engine.setReplayCount(3).ok);

    CanFrame f = frame2047(3);
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(3, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0xAA, driver.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, driver.sent[1].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, driver.sent[2].data[1]);
}

void test_replay_count_does_not_repeat_non_2047_frames()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"NonGTW","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("NonGTW", true).ok);
    TEST_ASSERT_TRUE(engine.setReplayCount(5).ok);

    CanFrame f = frame921();
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
}

void test_emit_periodic_requires_gtw2047_mux3()
{
    DashPluginEngine engine;
    DashPluginResult bad = engine.installJson(R"json({"name":"BadPeriodic","rules":[{"id":921,"mux":3,"ops":[{"type":"emit_periodic","interval":100}],"send":true}]})json", false);
    TEST_ASSERT_FALSE(bad.ok);

    DashPluginResult badMux = engine.installJson(R"json({"name":"BadPeriodicMux","rules":[{"id":2047,"mux":2,"mux_mask":255,"ops":[{"type":"emit_periodic","interval":100}],"send":true}]})json", false);
    TEST_ASSERT_FALSE(badMux.ok);

    DashPluginResult good = engine.installJson(R"json({"name":"GoodPeriodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"emit_periodic","interval":100}],"send":true}]})json", false);
    TEST_ASSERT_TRUE(good.ok);
}

void test_gtw_silent_unavailable_without_custom_key()
{
    DashPluginEngine engine;
    DashPluginResult result = engine.installJson(R"json({"name":"Silent","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"emit_periodic","interval":100,"gtw_silent":true}],"send":true}]})json", false);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(engine.statusJson().indexOf("custom key build required") >= 0);
}

void test_disabling_periodic_plugin_clears_cached_frame()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Periodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":17},{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Periodic", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    TEST_ASSERT_TRUE(engine.setEnabled("Periodic", false).ok);
    engine.tickPeriodic(10, allowedContext(), driver);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_replacing_plugin_clears_old_periodic_cache()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"ReplacePeriodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":34},{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("ReplacePeriodic", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"ReplacePeriodic","version":"2.0","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}],"send":true}]})json", false).ok);
    engine.tickPeriodic(10, allowedContext(), driver);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_gate_failure_clears_periodic_cache_before_next_allowed_tick()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"GatePeriodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":51},{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("GatePeriodic", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    DashPluginContext blocked = allowedContext();
    blocked.apGateAllowed = false;
    engine.tickPeriodic(10, blocked, driver);
    engine.tickPeriodic(20, allowedContext(), driver);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_checksum_recomputed_after_payload_ops_even_when_byte7_owned()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"High","rules":[{"id":921,"ops":[{"type":"set_byte","byte":7,"val":0}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Low","rules":[{"id":921,"ops":[{"type":"set_byte","byte":1,"val":170},{"type":"checksum"}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("High", true).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Low", true).ok);
    TEST_ASSERT_TRUE(engine.setPriority("High", 1).ok);
    TEST_ASSERT_TRUE(engine.setPriority("Low", 2).ok);

    CanFrame f = frame921();
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0xAA, driver.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[0]), driver.sent[0].data[7]);
}

void test_same_plugin_can_apply_multiple_ops_to_same_bit_without_priority_overlap()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"SamePlugin","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13,"val":1},{"type":"set_bit","bit":13,"val":0}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("SamePlugin", true).ok);

    CanFrame f = frame921();
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_FALSE((driver.sent[0].data[1] & 0x20) != 0);
    TEST_ASSERT_TRUE(engine.statusJson().indexOf("Priority overlap") < 0);
}

void test_emit_periodic_caches_final_rule_frame()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"FinalPeriodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"emit_periodic","interval":10},{"type":"set_byte","byte":1,"val":68},{"type":"checksum"}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("FinalPeriodic", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    engine.tickPeriodic(10, allowedContext(), driver);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x44, driver.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[0]), driver.sent[0].data[7]);
}

void test_blocked_apply_to_frame_clears_periodic_cache()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"ApplyGatePeriodic","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":85},{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("ApplyGatePeriodic", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    DashPluginContext blocked = allowedContext();
    blocked.canActive = false;
    DashPluginApplyResult blockedResult = engine.applyToFrame(f, blocked, driver);
    TEST_ASSERT_FALSE(blockedResult.sent);

    engine.tickPeriodic(10, allowedContext(), driver);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_disabling_non_emitter_plugin_clears_periodic_cache()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Mutator","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":2,"val":102}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Emitter","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Mutator", true).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Emitter", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    TEST_ASSERT_TRUE(engine.setEnabled("Mutator", false).ok);
    engine.tickPeriodic(10, allowedContext(), driver);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
}

void test_gtw2047_replay_advances_counter_and_checksum_per_copy()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"ReplayCounter","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"counter","byte":1,"mask":15,"step":1},{"type":"checksum"}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("ReplayCounter", true).ok);
    TEST_ASSERT_TRUE(engine.setReplayCount(3).ok);

    CanFrame f = frame2047(3);
    f.data[1] = 0x00;
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(3, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, driver.sent[0].data[1] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x02, driver.sent[1].data[1] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x03, driver.sent[2].data[1] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[0]), driver.sent[0].data[7]);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[1]), driver.sent[1].data[7]);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[2]), driver.sent[2].data[7]);
}

void test_periodic_send_advances_counter_and_checksum_each_tick()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"PeriodicCounter","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"counter","byte":1,"mask":15,"step":1},{"type":"checksum"},{"type":"emit_periodic","interval":10}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("PeriodicCounter", true).ok);

    CanFrame f = frame2047(3);
    TEST_ASSERT_TRUE(engine.applyToFrame(f, allowedContext(), driver).sent);
    driver.reset();

    engine.tickPeriodic(10, allowedContext(), driver);
    engine.tickPeriodic(20, allowedContext(), driver);

    TEST_ASSERT_EQUAL(2, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x02, driver.sent[0].data[1] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x03, driver.sent[1].data[1] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[0]), driver.sent[0].data[7]);
    TEST_ASSERT_EQUAL_HEX8(pluginTestChecksumByte(driver.sent[1]), driver.sent[1].data[7]);
}

void test_gtw2047_no_checksum_op_preserves_byte7_on_first_and_replay_sends()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"NoChecksumReplay","rules":[{"id":2047,"mux":3,"mux_mask":255,"ops":[{"type":"set_byte","byte":1,"val":34}],"send":true}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("NoChecksumReplay", true).ok);
    TEST_ASSERT_TRUE(engine.setReplayCount(3).ok);

    CanFrame f = frame2047(3);
    f.data[7] = 0xA5;
    DashPluginApplyResult applied = engine.applyToFrame(f, allowedContext(), driver);

    TEST_ASSERT_TRUE(applied.sent);
    TEST_ASSERT_EQUAL(3, driver.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0xA5, driver.sent[0].data[7]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, driver.sent[1].data[7]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, driver.sent[2].data[7]);
}

void test_export_import_preserves_enabled_priority_and_replay_count()
{
    DashPluginEngine engine;
    TEST_ASSERT_TRUE(engine.installJson(R"json({"name":"Persisted","rules":[{"id":921,"ops":[{"type":"set_bit","bit":13}]}]})json", false).ok);
    TEST_ASSERT_TRUE(engine.setEnabled("Persisted", true).ok);
    TEST_ASSERT_TRUE(engine.setReplayCount(4).ok);
    std::string saved = engine.exportConfigJson();

    DashPluginEngine restored;
    TEST_ASSERT_TRUE(restored.importConfigJson(saved.c_str()).ok);

    TEST_ASSERT_EQUAL(1, restored.pluginCount());
    TEST_ASSERT_TRUE(restored.pluginAt(0).enabled);
    TEST_ASSERT_EQUAL(4, restored.replayCount());
    // sourceJson round-trip fidelity: re-exporting the restored engine must
    // reproduce the same persisted document, proving the verbatim install
    // source survives export -> import -> export byte-for-byte (this is the
    // whole point of capturing sourceJson, so assert it directly).
    TEST_ASSERT_EQUAL_STRING(saved.c_str(), restored.exportConfigJson().c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_official_plugin_installs_disabled_by_default);
    RUN_TEST(test_invalid_json_is_rejected_without_installing);
    RUN_TEST(test_name_at_limit_is_accepted);
    RUN_TEST(test_name_longer_than_max_chars_is_rejected);
    RUN_TEST(test_installing_same_name_replaces_existing_plugin_disabled);
    RUN_TEST(test_disabled_plugin_does_not_send);
    RUN_TEST(test_enabled_plugin_applies_set_bit_and_sends_once);
    RUN_TEST(test_safety_context_blocks_plugin_execution);
    RUN_TEST(test_priority_prevents_lower_plugin_from_overwriting_same_bit);
    RUN_TEST(test_gtw2047_replay_repeats_only_modified_2047_frames);
    RUN_TEST(test_replay_count_does_not_repeat_non_2047_frames);
    RUN_TEST(test_emit_periodic_requires_gtw2047_mux3);
    RUN_TEST(test_gtw_silent_unavailable_without_custom_key);
    RUN_TEST(test_disabling_periodic_plugin_clears_cached_frame);
    RUN_TEST(test_replacing_plugin_clears_old_periodic_cache);
    RUN_TEST(test_gate_failure_clears_periodic_cache_before_next_allowed_tick);
    RUN_TEST(test_checksum_recomputed_after_payload_ops_even_when_byte7_owned);
    RUN_TEST(test_same_plugin_can_apply_multiple_ops_to_same_bit_without_priority_overlap);
    RUN_TEST(test_emit_periodic_caches_final_rule_frame);
    RUN_TEST(test_blocked_apply_to_frame_clears_periodic_cache);
    RUN_TEST(test_disabling_non_emitter_plugin_clears_periodic_cache);
    RUN_TEST(test_gtw2047_replay_advances_counter_and_checksum_per_copy);
    RUN_TEST(test_periodic_send_advances_counter_and_checksum_each_tick);
    RUN_TEST(test_gtw2047_no_checksum_op_preserves_byte7_on_first_and_replay_sends);
    RUN_TEST(test_export_import_preserves_enabled_priority_and_replay_count);
    return UNITY_END();
}
