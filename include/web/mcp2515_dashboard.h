#pragma once

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#endif
#include <esp_task_wdt.h>
#ifdef ESP_PLATFORM
#include <driver/temperature_sensor.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_pm.h>
#include <esp_spiffs.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_private/esp_clk.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#ifndef ESP_PLATFORM
#include <Preferences.h>
#include <SPIFFS.h>
#endif
#include "handlers.h"
#include "can_helpers.h"
#include <ArduinoJson.h>
#if defined(DRIVER_ESP32_EXT_MCP2515)
#include "drivers/esp32_mcp2515_driver.h"
#endif
#include "web/mcp2515_dashboard_ui.h"
#include "dash_ota_guard.h"
#include "dash_power_mgmt.h"
#include "dash_fog_light.h"
#include "dash_wheel_dnd.h"
#include "dash_tx_evidence.h"
#include "dash_capabilities.h"
#include "dash_twai_diag.h"
#if defined(DASH_PLUGIN_ENGINE)
#include "dash_plugin_engine.h"
#endif
#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI) && !defined(NATIVE_BUILD)
#include "drivers/twai_driver.h"
#endif

#ifndef DASH_SSID
#error "Define -DDASH_SSID in build_flags (e.g. -DDASH_SSID=\\\"ADUnlock-1234\\\")"
#endif
#ifndef DASH_PASS
#error "Define -DDASH_PASS in build_flags (min 8 chars)"
#endif
#ifndef DASH_OTA_PASS
#error "Define -DDASH_OTA_PASS in build_flags"
#endif
#ifndef DASH_OTA_USER
#error "Define -DDASH_OTA_USER in build_flags"
#endif

static_assert(sizeof(DASH_SSID) > 1 && sizeof(DASH_SSID) <= 33, "DASH_SSID must be 1-32 bytes");
static_assert(sizeof(DASH_PASS) >= 9 && sizeof(DASH_PASS) <= 65, "DASH_PASS must be 8-64 bytes");

#ifndef DASH_DEFAULT_HW
#define DASH_DEFAULT_HW 1
#endif
#ifndef DASH_BUILD_ENV
#define DASH_BUILD_ENV "unknown"
#endif
#ifndef DASH_UI_BUILD_ID
#define DASH_UI_BUILD_ID "unknown"
#endif
#ifndef DASH_UI_BUILD_UTC
#define DASH_UI_BUILD_UTC "unknown"
#endif

#if defined(DASH_INJECTION_ON_BOOT)
static constexpr bool kDashInjectionDefaultEnabled = true;
#else
static constexpr bool kDashInjectionDefaultEnabled = false;
#endif

#if defined(DRIVER_TWAI)
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_5
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_4
#endif
#endif

#if DASH_DEFAULT_HW < 0 || DASH_DEFAULT_HW > 2
#error "DASH_DEFAULT_HW must be 0 (LEGACY), 1 (HW3), or 2 (HW4)"
#endif

#define PREFS_NS "ADunlock"
static constexpr uint8_t kDashUnsetU8 = 0xFF;

static Preferences prefs;

static CarManagerBase *dashHandler = nullptr;
static CanDriver *dashDriver = nullptr;
#if defined(DRIVER_ESP32_EXT_MCP2515)
static MCP2515 *dashMcp = nullptr;
#endif

static unsigned long rxCount = 0;
static unsigned long txCount = 0;
static unsigned long txErrCount = 0;
static unsigned long lastFrameMs = 0;
static unsigned long startMs = 0;
static bool canOnline = false;
static uint8_t followDist = 0;

static unsigned long fpsFrames = 0;
static unsigned long fpsLastMs = 0;
static float fps = 0.0f;

static unsigned long muxRx[4] = {};
static unsigned long muxTx[4] = {};
static unsigned long muxErr[4] = {};

#if defined(DRIVER_ESP32_EXT_MCP2515)
static uint8_t mcpEflg = 0;
#else
static const uint8_t mcpEflg = 0;
#endif

#ifndef DASH_INITIAL_HW_MODE
#define DASH_INITIAL_HW_MODE DASH_DEFAULT_HW
#endif
static uint8_t hwMode = DASH_INITIAL_HW_MODE;
static bool canActive = kDashInjectionDefaultEnabled;
static bool forceActivate = false;
static bool bootCanActive = kDashInjectionDefaultEnabled;
#if defined(DASH_PLUGIN_ENGINE)
static DashPluginEngine dashPluginEngine;
static String dashPluginUploadBuffer;
#endif
#ifndef DASH_AP_GATE_DEFAULT
#if defined(INJECTION_AFTER_AP) || defined(DASH_INJECTION_AFTER_AP)
#define DASH_AP_GATE_DEFAULT true
#else
#define DASH_AP_GATE_DEFAULT false
#endif
#endif

// AP Injection Gate — when false (default), 1021 mux0 bit46 注入与车辆状态解耦，
// 复刻 2.5.2 真车固件默认行为（DASH_AP_GATE_DEFAULT=false）。
// 当 true 时回到 3.0 早期行为：必须 Parked||APActive||Summoning 才允许注入。
static bool apInjectionGate = DASH_AP_GATE_DEFAULT;
static bool apAutoRestore = false;
// 上一次 dashPostProcessFrame 实际发送成功的时间戳，便于 /status 区分"在持续发"与
// "发了几次就停"，与 framesSent 单调累计计数互补。跨 CAN 任务 / dashboard 任务读写。
static volatile uint32_t lastInjectMs = 0;
// Legacy FSD (0x3EE mux0) AP-settle activation gate — upstream beta3 parity.
// Legacy injection must wait ~2 s after APActive first rises (and CAN must be
// active, OTA not blocking, apInjectionGate open) before mux0 bit46 fires.
// AP settle delay is configurable (0-3000 ms, default 2000) so the standalone
// single-CAN build can expose it through /config and persist across reboots.
static constexpr uint32_t kLegacyFsdActivationSettleDefaultMs = 2000;
static constexpr uint32_t kLegacyFsdActivationSettleMinMs = 0;
static constexpr uint32_t kLegacyFsdActivationSettleMaxMs = 3000;
// Soft Engage (upstream flipper-tesla-fsd v2.16-beta.10 alignment): once AP
// has settled, additionally hold the Legacy 0x3EE bit46 activation-edge
// injection until the steering wheel is near-centred (or the timeout fires),
// then latch for the rest of the episode. Reduces curve-entry jerk.
static constexpr bool kSoftEngageDefaultEnabled = true;     // ON: Jordan is the 8.3.6 jerk owner
static constexpr int SOFT_ENGAGE_ANGLE_THRESH_X10 = 50;     // ±5.0° (conservative; on-car tune)
static constexpr uint32_t SOFT_ENGAGE_TIMEOUT_MS = 5000;    // long-curve fallback (never strand driver)
static uint32_t legacyFsdApActiveSinceMs = 0;
static uint32_t legacyFsdRequiredStableMs = kLegacyFsdActivationSettleDefaultMs;
static uint32_t legacyFsdLastBlockedMs = 0;
static bool legacyFsdLastAllowed = false;
static bool dashSoftEngage = kSoftEngageDefaultEnabled;     // opt-in toggle (UI/NVS `def_se`)
static bool legacySoftEngageSent = false;                   // per-episode latch: first bit46 release
static bool dashSpeedProfileAuto = true;
static uint8_t dashManualSpeedProfile = 1;
static uint8_t dashDriveProfile = 0;  // 0=Auto, 1=Sloth, 2=Chill, 3=Normal, 4=Hurry, 5=MAX
static uint8_t dashSpeedStrategy = 1; // 0=fixed, 1=auto, 2=custom
static bool dashLightingEnabled = false;
static uint8_t dashLightingCount = 3;
static uint8_t dashLightingFrequency = 1; // 0=slow, 1=medium, 2=fast
static uint8_t dashRearFogStrategy = 0;   // 0=off, 1=strobe, 2=continuous
static DashFogLight dashFogCtrl;          // Phase 4 fog light controller instance
static bool dashFogOffRequested = false;  // one-shot fail-off/stop command owned by CAN task
static DashWheelDND dashWheelDndCtrl;     // Phase 3 wheel DND controller instance
static bool dashDefenseEnabled = false;
static bool dashBionicSteering = false;
static bool dashSpeedNoDisturb = false;
static bool dashDndVolume = false;      // 音量消除DND（Phase 3实现执行逻辑）
static bool dashDndSpeed = false;       // 速度滚轮DND（Phase 3实现执行逻辑）
static bool dashBionicDisabled = false; // bionic auto-disabled after 3 failures
static bool dashNagTorqueTamper = false; // OPT-IN 0x370 torque-tamper (1.80Nm). DEFAULT OFF.
static bool dashApEapCompatible = true;
static LegacyFsdPolicy dashLegacyFsdPolicy = LegacyFsdPolicy::Stable;
static bool dashLegacyFsdMux1Enable = false;
static bool dashLegacyFsdProfileWriteEnable = false;
static bool dashLegacyFsdVisionLimitClearEnable = false;

// HW3 slew limiter constants/state moved to include/dash_hw3_speed.h so
// HW3Handler can call dashApplyHw3OffsetSlew directly.

// HW3 custom-speed config + helpers live in their own header so handlers.h
// (parsed before this file) can read fusedSpeedLimitRaw and call the
// encoders. See include/dash_hw3_speed.h.

// --- FSD runtime switch NVS staging (loaded before handlerPool init) ---
static bool nvsAutoModeEnabled = false;
static bool nvsTlsscBypass = false;
static bool nvsEmergencyVehicleDetection = true;
static bool nvsIsaChimeSuppress = false;
static bool nvsIsaOverride = false;
static uint8_t nvsHw4OffsetRaw = 0;
static bool nvsBanShieldEnable = false;
static int nvsLegacyOffset = 0;
static bool nvsRemoveVisionSpeedLimit = true;
static bool nvsOverrideSpeedLimit = false;

#ifdef RGB_BRIGHTNESS
static constexpr uint8_t kDashLedBrightnessDefault = RGB_BRIGHTNESS;
#else
static constexpr uint8_t kDashLedBrightnessDefault = 32;
#endif
static constexpr uint8_t dashLedBrightness = kDashLedBrightnessDefault;

#if defined(DASH_SINGLE_CAN_STANDALONE)
static constexpr bool kDashSingleCanStandalone = true;
#else
static constexpr bool kDashSingleCanStandalone = false;
#endif

#if defined(DRIVER_T2CAN_DUAL) && !defined(DASH_SINGLE_CAN_STANDALONE)
static constexpr bool kDashCan2Available = true;
#else
static constexpr bool kDashCan2Available = false;
#endif

static constexpr bool kDashLightingBusSupported = kDashCan2Available;
static constexpr bool kDashServiceModeSupported = kDashCan2Available;
static constexpr bool kDashStalkTestSupported = kDashCan2Available;
static constexpr bool kDashBus2SnifferSupported = kDashCan2Available;

static String dashCapabilitiesJson()
{
    String j = "{";
    j += "\"";
    j += "singleCan";
    j += "\":"; // "singleCan":
    j += kDashSingleCanStandalone ? "true" : "false";
    j += ",\"";
    j += "can2Available";
    j += "\":"; // "can2Available":
    j += kDashCan2Available ? "true" : "false";
    j += ",\"";
    j += "lightingBusSupported";
    j += "\":"; // "lightingBusSupported":
    j += kDashLightingBusSupported ? "true" : "false";
    j += ",\"";
    j += "serviceModeSupported";
    j += "\":"; // "serviceModeSupported":
    j += kDashServiceModeSupported ? "true" : "false";
    j += ",\"";
    j += "stalkTestSupported";
    j += "\":"; // "stalkTestSupported":
    j += kDashStalkTestSupported ? "true" : "false";
    j += ",\"";
    j += "bus2SnifferSupported";
    j += "\":"; // "bus2SnifferSupported":
    j += kDashBus2SnifferSupported ? "true" : "false";
    j += ",\"";
    j += "fsdActivation";
    j += "\":true"; // "fsdActivation":true
    j += ",\"";
    j += "speedStrategy";
    j += "\":true"; // "speedStrategy":true
    j += ",\"";
    j += "driveProfile";
    j += "\":true"; // "driveProfile":true
    j += ",\"";
    j += "networkSettings";
    j += "\":true"; // "networkSettings":true
    j += ",\"";
    j += "otaUpdate";
    j += "\":true"; // "otaUpdate":true
    j += ",\"";
    j += "canDiagnostics";
    j += "\":true"; // "canDiagnostics":true
    j += ",\"";
    j += "pluginEngine";
    j += "\":";
#if defined(DASH_PLUGIN_ENGINE)
    j += "true";
#else
    j += "false";
#endif
    j += "}"; // close capabilities object
    return j;
}

// WiFi AP (hotspot) — overridable at runtime
static char apSSID[33] = "";
static char apPass[65] = "";
static bool apHidden = false; // when true, SSID is not broadcast (hidden AP)
static constexpr size_t kDashMaxSsidLen = 32;
static constexpr size_t kDashMinApPassLen = 8;
static constexpr size_t kDashMaxPassLen = 64;
static constexpr int kDashApChannel = 1;
static constexpr int kDashApMaxConn = 4;
static uint8_t apRuntimeChannel = kDashApChannel;
static unsigned long apLastChannelSyncMs = 0;
static uint8_t apLastChannelSyncTarget = 0;
static bool apLastChannelSyncOk = false;

// WiFi STA (client) mode for internet access
static char staSSID[33] = "";
static char staPass[65] = "";
static bool staConnected = false;
static bool staConnectAttemptActive = false;
static bool staStaticIP = false;

// Multi-SSID storage
static constexpr uint8_t kDashMaxWifiNetworks = 4;
struct DashWifiNetwork
{
    char ssid[33];
    char pass[65];
    bool useStatic;
    char ip[16];
    char gw[16];
    char mask[16];
    char dns[16];
};
static DashWifiNetwork wifiNetworks[kDashMaxWifiNetworks] = {};
static uint8_t wifiNetworkCount = 0;
static int8_t wifiActiveSlot = -1;    // slot currently selected for STA attempt
static int8_t wifiNextRotateSlot = 0; // next slot to try when rotating
static bool updateBetaChannel = false;
static bool autoUpdateEnabled = false;
static bool autoUpdateDone = false;            // one-shot per boot
static unsigned long autoUpdateEligibleAt = 0; // millis() at which auto-check may fire
static unsigned long staConnectStartedAt = 0;
static unsigned long staRetryAt = 0;
static uint8_t staConsecutiveFailures = 0; // diagnostics only; retry interval is fixed
static constexpr unsigned long kDashStaBootDelayMs = 1000;
static constexpr unsigned long kDashStaSavedPollMs = 5000;
static constexpr unsigned long kDashStaConnectTimeoutMs = 10000;
// kDashStaRetryMs kept for backward compat with older references.
static constexpr unsigned long kDashStaRetryMs = kDashStaSavedPollMs;
static IPAddress staIP(0, 0, 0, 0);
static IPAddress staGW(0, 0, 0, 0);
static IPAddress staMask(255, 255, 255, 0);
static IPAddress staDNS(0, 0, 0, 0);

// Multi-SSID NVS helpers (key form: w0s, w0p, w0t, w0i, w0g, w0m, w0d)
static String dashWifiKey(uint8_t slot, const char *sub)
{
    String k = "w";
    k += slot;
    k += sub;
    return k;
}
static void dashClearWifiNetwork(DashWifiNetwork &n)
{
    n.ssid[0] = 0;
    n.pass[0] = 0;
    n.useStatic = false;
    n.ip[0] = 0;
    n.gw[0] = 0;
    n.mask[0] = 0;
    n.dns[0] = 0;
}
static void dashRotateAndConnect();
static void dashSwapHandler(uint8_t mode);
static void dashApplyFilters();
static void dashApplyRuntimeState();
static void dashClearLegacyOptionPrefs();
static void dashLog(const String &s);
static const char *dashWifiStatusName(int status);

// CAN recorder
#ifndef REC_CAP
// Real-car: 8000 frames (~560KB) written to a filling SPIFFS made /rec_stop
// block until the browser fetch timed out ("cannot stop recording"). 2000
// (~140KB) saves fast and slows SPIFFS accumulation.
#define REC_CAP 2000
#endif
static constexpr unsigned long kRecMaxDurationMs = 60000UL;
struct RecFrame
{
    unsigned long ts;
    char dir;
    uint32_t id;
    uint8_t dlc;
    uint8_t bus;
    uint8_t data[8];
};
static RecFrame *recBuf = nullptr;
static bool recBufInPsram = false;
static volatile bool recActive = false;
static volatile int recCount = 0;
static bool recSaved = false;
static unsigned long recStartMs = 0;

// Optional capture filter: when recFilterCount > 0, only frames whose ID is in
// recFilterIds are recorded. Empty (0) = record everything (default behaviour).
// Lets you capture just the lighting/stalk IDs (0x249/0x3E9/0x3F5) over a long
// window without the busy primary bus flooding the buffer.
static constexpr int kRecFilterMax = 16;
static uint32_t recFilterIds[kRecFilterMax];
static int recFilterCount = 0;

// CAN sniffer ring buffer
#define SNIFFER_CAP 30
struct SniffFrame
{
    unsigned long ts;
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
};
static SniffFrame sniffBuf[SNIFFER_CAP];
static int sniffHead = 0;
static int sniffCount = 0;

enum DashWriteProbeState : uint8_t
{
    kDashWriteProbeIdle = 0,
    kDashWriteProbePending = 1,
    kDashWriteProbeMatch = 2,
    kDashWriteProbeDifferent = 3,
    kDashWriteProbeFailed = 4,
};

struct DashWriteProbe
{
    bool active = false;
    bool hasRx = false;
    uint8_t state = kDashWriteProbeIdle;
    uint32_t id = 0;
    int8_t mux = -1;
    uint8_t txDlc = 0;
    uint8_t rxDlc = 0;
    uint8_t txData[8] = {};
    uint8_t rxData[8] = {};
    unsigned long txMs = 0;
    unsigned long rxMs = 0;
};
static DashWriteProbe dashWriteProbe;

struct DashApRestoreState
{
    bool gearSeen = false;
    uint8_t gearRaw = 0xFF;
    unsigned long gearMs = 0;
    bool brakeSeen = false;
    uint8_t brakePedalRaw = 0xFF;
    bool chassisSeen = false;
    bool brakeTorqueActive = false;
    uint8_t anyVdcActive = 0xFF;
    bool tcActive = false;
    uint8_t vdcControlActive = 0xFF;
    bool steerSeen = false;
    uint8_t steerValidity = 0xFF;
    int16_t steerAngleX10 = 0;
    int16_t steerSpeedX10 = 0;
    unsigned long steerMs = 0;
    bool dasSettingsSeen = false;
    uint8_t dasSettingsData[8] = {};
    unsigned long dasSettingsMs = 0;
    uint8_t dasSettingsCounter = 0xFF;
    bool dasAccSeen = false;
    uint8_t dasAccState = 0xFF;
    unsigned long dasAccDropMs = 0;
    unsigned long lastDropHandledMs = 0;
    unsigned long lastTxMs = 0;
};
static DashApRestoreState apRestoreState;
static constexpr unsigned long kDashApRestoreTxCooldownMs = 1000;

static int8_t dashFrameMux(const CanFrame &frame)
{
    if ((frame.id == 1006 || frame.id == 1021) && frame.dlc > 0)
        return static_cast<int8_t>(readMuxID(frame));
    return -1;
}

static uint32_t dashReadBitsLE(const CanFrame &frame, uint8_t startBit, uint8_t bitCount)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < bitCount; i++)
    {
        uint8_t bit = startBit + i;
        if (bit >= frame.dlc * 8)
            break;
        if (frame.data[bit / 8] & (1U << (bit % 8)))
            value |= 1UL << i;
    }
    return value;
}

static bool dashReadBit(const CanFrame &frame, uint8_t bit)
{
    return dashReadBitsLE(frame, bit, 1) != 0;
}

static uint8_t dashCounterChecksumByte(const CanFrame &frame, uint8_t checksumByteIndex = 7)
{
    if (checksumByteIndex >= frame.dlc)
        return 0;
    uint16_t sum = static_cast<uint16_t>(frame.id & 0xFF) +
                   static_cast<uint16_t>((frame.id >> 8) & 0xFF);
    for (uint8_t i = 0; i < frame.dlc; i++)
    {
        if (i == checksumByteIndex)
            sum += frame.data[i] & 0x0F;
        else
            sum += frame.data[i];
    }
    uint8_t checksum = static_cast<uint8_t>((0x10 - (sum & 0x0F)) & 0x0F);
    return static_cast<uint8_t>((checksum << 4) | (frame.data[checksumByteIndex] & 0x0F));
}

static void dashResetWriteProbe()
{
    dashWriteProbe = {};
    dashWriteProbe.mux = -1;
    dashWriteProbe.state = kDashWriteProbeIdle;
}

static bool dashEnsureRecBuffer()
{
    if (recBuf)
        return true;
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    recBuf = static_cast<RecFrame *>(heap_caps_calloc(REC_CAP, sizeof(RecFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (recBuf)
    {
        recBufInPsram = true;
        Serial.println("[REC] Buffer allocated in PSRAM");
        return true;
    }
#endif
    recBuf = static_cast<RecFrame *>(heap_caps_calloc(REC_CAP, sizeof(RecFrame), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    recBufInPsram = false;
    if (!recBuf)
    {
        Serial.println("[REC] Buffer allocation failed");
        return false;
    }
    Serial.println("[REC] Buffer allocated in internal RAM");
    return true;
}

static void dashReleaseRecBuffer()
{
    if (!recBuf || recActive)
        return;
    heap_caps_free(recBuf);
    recBuf = nullptr;
    recBufInPsram = false;
}

static bool dashSaveRecordingToSpiffs(int n)
{
    if (!recBuf)
    {
        dashLog("[REC] Save failed: buffer unavailable");
        return false;
    }
    if (n < 0)
        n = 0;
    if (n > REC_CAP)
        n = REC_CAP;

    // SPIFFS is log-structured: explicit remove frees stale blocks from prior
    // recordings (real-car SPIFFS had accumulated to 86% full, slowing saves).
    SPIFFS.remove("/rec.csv");
    File f = SPIFFS.open("/rec.csv", "w");
    if (!f)
    {
        dashLog("[REC] SPIFFS write failed");
        return false;
    }

    // bus: 2 = secondary MCP2515 (X197 9/10), 1 = primary TWAI (X197 13/14)
    f.println("ts_ms,dir,bus,id,dlc,b0,b1,b2,b3,b4,b5,b6,b7");
    char line[96];
    for (int i = 0; i < n; i++)
    {
        int len = snprintf(line, sizeof(line),
                           "%lu,%c,%u,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                           recBuf[i].ts,
                           recBuf[i].dir ? recBuf[i].dir : 'R',
                           static_cast<unsigned>(recBuf[i].bus == CAN_BUS_PARTY ? 2 : 1),
                           static_cast<unsigned long>(recBuf[i].id),
                           static_cast<unsigned>(recBuf[i].dlc),
                           static_cast<unsigned>(recBuf[i].data[0]),
                           static_cast<unsigned>(recBuf[i].data[1]),
                           static_cast<unsigned>(recBuf[i].data[2]),
                           static_cast<unsigned>(recBuf[i].data[3]),
                           static_cast<unsigned>(recBuf[i].data[4]),
                           static_cast<unsigned>(recBuf[i].data[5]),
                           static_cast<unsigned>(recBuf[i].data[6]),
                           static_cast<unsigned>(recBuf[i].data[7]));
        if (len > 0)
            f.write(reinterpret_cast<const uint8_t *>(line),
                    static_cast<size_t>(len < static_cast<int>(sizeof(line)) ? len : sizeof(line) - 1));
    }
    f.close();
    recSaved = true;
    dashLog("[REC] Saved " + String(n) + " frames to SPIFFS");
    return true;
}

static bool dashStopRecordingAndSave(const char *reason = nullptr)
{
    if (!recActive && recSaved)
        return true;
    recActive = false;
    int n = recCount;
    bool ok = dashSaveRecordingToSpiffs(n);
    if (reason && *reason)
        dashLog(String("[REC] Stopped: ") + reason);
    return ok;
}

static void dashRecordCanFrame(const CanFrame &f, char dir)
{
    if (!recActive || !recBuf)
        return;
    if (recFilterCount > 0)
    {
        bool match = false;
        uint32_t fid = f.id & 0x1FFFFFFFUL;
        for (int k = 0; k < recFilterCount; k++)
        {
            if (recFilterIds[k] == fid)
            {
                match = true;
                break;
            }
        }
        if (!match)
            return;
    }
    int idx = recCount;
    if (idx >= REC_CAP)
        return;
    uint8_t dlc = (f.dlc <= 8) ? f.dlc : 8;
    recBuf[idx].ts = millis();
    recBuf[idx].dir = dir;
    recBuf[idx].id = f.id;
    recBuf[idx].dlc = dlc;
    recBuf[idx].bus = f.bus;
    memset(recBuf[idx].data, 0, sizeof(recBuf[idx].data));
    memcpy(recBuf[idx].data, f.data, dlc);
    recCount = idx + 1;
    if (recCount >= REC_CAP)
        dashStopRecordingAndSave("frame limit");
}

static void dashRecordApRestoreFrame(const CanFrame &frame, unsigned long now)
{
    if (frame.id == 280 && frame.dlc >= 3)
    {
        apRestoreState.gearSeen = true;
        apRestoreState.gearRaw = readDIGear(frame);
        apRestoreState.gearMs = now;
        apRestoreState.brakeSeen = true;
        apRestoreState.brakePedalRaw = static_cast<uint8_t>(dashReadBitsLE(frame, 19, 2));
        return;
    }
    if (frame.id == 0x148 && frame.dlc >= 8)
    {
        apRestoreState.chassisSeen = true;
        apRestoreState.brakeTorqueActive = dashReadBit(frame, 15);
        apRestoreState.anyVdcActive = static_cast<uint8_t>(dashReadBitsLE(frame, 34, 2));
        apRestoreState.tcActive = dashReadBit(frame, 42);
        apRestoreState.vdcControlActive = static_cast<uint8_t>(dashReadBitsLE(frame, 60, 3));
        return;
    }
    if (frame.id == 0x129 && frame.dlc >= 6)
    {
        uint16_t angleRaw = static_cast<uint16_t>(dashReadBitsLE(frame, 16, 14));
        uint16_t speedRaw = static_cast<uint16_t>(dashReadBitsLE(frame, 32, 14));
        apRestoreState.steerSeen = true;
        apRestoreState.steerValidity = static_cast<uint8_t>(dashReadBitsLE(frame, 30, 2));
        apRestoreState.steerAngleX10 = angleRaw == 0x3FFF ? 0 : static_cast<int16_t>(angleRaw) - 8192;
        apRestoreState.steerSpeedX10 = static_cast<int16_t>(static_cast<int32_t>(speedRaw) * 5 - 40960);
        apRestoreState.steerMs = now;
        return;
    }
    if (frame.id == 0x293 && frame.dlc >= 8)
    {
        apRestoreState.dasSettingsSeen = true;
        apRestoreState.dasSettingsMs = now;
        memcpy(apRestoreState.dasSettingsData, frame.data, 8);
        apRestoreState.dasSettingsCounter = frame.data[7] & 0x0F;
        return;
    }
    if (frame.id == 0x389 && frame.dlc >= 4)
    {
        uint8_t accState = static_cast<uint8_t>((frame.data[3] >> 2) & 0x1F);
        if (apRestoreState.dasAccSeen && apRestoreState.dasAccState > 0 && accState == 0)
            apRestoreState.dasAccDropMs = now;
        apRestoreState.dasAccSeen = true;
        apRestoreState.dasAccState = accState;
    }
}

static bool dashWriteProbeMatches(const CanFrame &frame)
{
    if (!dashWriteProbe.active || dashWriteProbe.id != frame.id)
        return false;

    int8_t mux = dashFrameMux(frame);
    if (dashWriteProbe.mux < 0)
        return mux < 0;
    return mux == dashWriteProbe.mux;
}

static const char *decodeCanId(uint32_t id)
{
    switch (id)
    {
    case 0x045:
        return "STW_ACTN_RQ";
    case 0x129:
        return "Steering angle";
    case 0x175:
        return "Speed";
    case 0x186:
        return "Gear/Drive state";
    case 0x118:
        return "DI_systemStatus";
    case 0x233:
        return "UI_stalklessControl";
    case 0x257:
        return "State of charge";
    case 0x293:
        return "DAS control";
    case 0x321:
        return "Autopilot state";
    case 0x329:
        return "UI_autopilot";
    case 0x399:
        return "DAS_status";
    case 0x3E8:
        return "UI_driverAssistControl";
    case 0x3FD:
        return "UI_autopilotControl";
    case 0x678:
        return "GTW_gearControl";
    default:
        return "";
    }
}

static void sniffPush(const CanFrame &f)
{
    uint8_t dlc = (f.dlc <= 8) ? f.dlc : 8;
    sniffBuf[sniffHead] = {millis(), f.id, dlc, {}};
    memcpy(sniffBuf[sniffHead].data, f.data, dlc);
    sniffHead = (sniffHead + 1) % SNIFFER_CAP;
    if (sniffCount < SNIFFER_CAP)
        sniffCount++;
}

#define LOG_CAP 80
struct LogEntry
{
    String msg;
    unsigned long seq;
};
static LogEntry logBuf[LOG_CAP];
static int logHead = 0;
static int logCount = 0;
static unsigned long logSeq = 0;
// Cursor tracking how much of logRing we have copied into logBuf so far.
// logRing is filled by HW3/HW4 handlers when enablePrint is on (per-frame
// "AD: 1, Profile: 2, Offset: ..." diagnostics). We drain it here on demand
// so the WebUI /log endpoint surfaces those diagnostics in real time.
static uint32_t logRingDrainCursor = 0;

static void dashLog(const String &s)
{
    logBuf[logHead] = {String(millis() / 1000) + "s " + s, ++logSeq};
    logHead = (logHead + 1) % LOG_CAP;
    if (logCount < LOG_CAP)
        logCount++;
    Serial.println(s);
}

// Pull all new entries from the per-frame handler logRing (in handlers.h)
// into logBuf so /log returns them. Cheap: bounded by ring capacity (32).
static void dashDrainLogRing()
{
    uint32_t h = logRing.currentHead();
    if (h <= logRingDrainCursor)
    {
        logRingDrainCursor = h; // handle wrap / restart
        return;
    }
    LogRingBuffer::Entry tmp[LogRingBuffer::kCapacity];
    int n = logRing.readSince(logRingDrainCursor, tmp, LogRingBuffer::kCapacity);
    for (int i = 0; i < n; i++)
    {
        // Use the timestamp captured at push time, not now, so messages keep
        // their actual ordering. dashLog format prefixes seconds-since-boot.
        logBuf[logHead] = {String(tmp[i].timestamp_ms / 1000) + "s " + String(tmp[i].msg), ++logSeq};
        logHead = (logHead + 1) % LOG_CAP;
        if (logCount < LOG_CAP)
            logCount++;
    }
    logRingDrainCursor = h;
}

// Public hooks
static void mcpDashOnFrame(const CanFrame &f)
{
    unsigned long now = millis();
    rxCount++;
    lastFrameMs = now;
    canOnline = true;
    fpsFrames++;
    sniffPush(f);
    if (f.id == 1021 && f.dlc > 0)
    {
        uint8_t m = f.data[0] & 0x07;
        if (m < 4)
            muxRx[m]++;
    }
    if (f.id == 1016 && f.dlc > 5)
        followDist = (f.data[5] & 0xE0) >> 5;
    dashRecordApRestoreFrame(f, now);
    dashRecordCanFrame(f, 'R');
    // Phase 1: OTA guard 检测 0x318 帧
    dashOtaGuardProcessFrame(f);
    // Phase 1: 功耗管理 — 记录 CAN 活动
    dashPowerMgmtTouchCan();
    if (dashWriteProbe.active && dashWriteProbe.state != kDashWriteProbeFailed && dashWriteProbeMatches(f))
    {
        dashWriteProbe.hasRx = true;
        dashWriteProbe.rxMs = now;
        dashWriteProbe.rxDlc = (f.dlc <= 8) ? f.dlc : 8;
        memset(dashWriteProbe.rxData, 0, sizeof(dashWriteProbe.rxData));
        memcpy(dashWriteProbe.rxData, f.data, dashWriteProbe.rxDlc);
        bool same = dashWriteProbe.txDlc == dashWriteProbe.rxDlc &&
                    memcmp(dashWriteProbe.txData, dashWriteProbe.rxData, dashWriteProbe.txDlc) == 0;
        dashWriteProbe.state = same ? kDashWriteProbeMatch : kDashWriteProbeDifferent;
    }
}

static void mcpDashOnTxFrame(const CanFrame &frame, bool ok)
{
    txCount++;
    int8_t mux = dashFrameMux(frame);
    if (!ok)
    {
        txErrCount++;
        if (mux >= 0 && mux < 4)
            muxErr[mux]++;
    }
    else if (mux >= 0 && mux < 4)
    {
        muxTx[mux]++;
    }
    if (ok)
        dashRecordCanFrame(frame, 'T');

    dashWriteProbe.active = true;
    dashWriteProbe.hasRx = false;
    dashWriteProbe.state = ok ? kDashWriteProbePending : kDashWriteProbeFailed;
    dashWriteProbe.id = frame.id;
    dashWriteProbe.mux = mux;
    dashWriteProbe.txMs = millis();
    dashWriteProbe.rxMs = 0;
    dashWriteProbe.txDlc = (frame.dlc <= 8) ? frame.dlc : 8;
    dashWriteProbe.rxDlc = 0;
    memset(dashWriteProbe.txData, 0, sizeof(dashWriteProbe.txData));
    memset(dashWriteProbe.rxData, 0, sizeof(dashWriteProbe.rxData));
    memcpy(dashWriteProbe.txData, frame.data, dashWriteProbe.txDlc);
}

// JSON escape for log strings
static String jsonEscape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (unsigned int i = 0; i < s.length(); i++)
    {
        char c = s.charAt(i);
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out;
}

static bool dashApInjectionAllowed();
static FsdGateBlockReason dashCurrentGateBlockReason();

static bool dashCheckADEnabled()
{
    return canActive && dashOtaGuardAllowInjection() && dashApInjectionAllowed();
}

static bool dashApInjectionAllowed()
{
    // 2.5.2 风格：apInjectionGate=false 时短路放行；=true 时回到 3.0 强制门控。
    return !apInjectionGate || (dashHandler && dashHandler->injectionGateOpen());
}

// Legacy FSD (0x3EE mux0) activation gate with a ~2 s AP-settle hold-off.
// Mirrors upstream ev-open-can-tools v3.0.2-beta.3 safety parity: once AP
// becomes active (and CAN/OTA/gate all allow), hold off legacy mux0 bit46
// injection until APActive has been continuously asserted for
// legacyFsdRequiredStableMs. Any loss of CAN / OTA / gate / APActive
// resets the settle timer. nowMs is the same monotonically-increasing
// millisecond counter the handler path uses (millis() in firmware,
// dashDiagNowMs() in native tests) so the hold-off interval is consistent.
static bool dashLegacyFsdActivationAllowed(uint32_t nowMs)
{
    if (!canActive)
    {
        legacyFsdApActiveSinceMs = 0;
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false;
    }
    if (!dashOtaGuardAllowInjection())
    {
        legacyFsdApActiveSinceMs = 0;
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false;
    }
    if (!apInjectionGate)
    {
        legacyFsdLastAllowed = true;
        return true;
    }
    bool apActive = dashHandler && (bool)dashHandler->APActive;
    if (!apActive)
    {
        legacyFsdApActiveSinceMs = 0;
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false;
    }
    if (legacyFsdApActiveSinceMs == 0)
    {
        legacyFsdApActiveSinceMs = nowMs;
        legacySoftEngageSent = false; // new AP episode → re-arm soft-engage latch
    }
    const bool stable = (nowMs - legacyFsdApActiveSinceMs) >= legacyFsdRequiredStableMs;
    const bool timeout = (nowMs - legacyFsdApActiveSinceMs)
                         >= (legacyFsdRequiredStableMs + SOFT_ENGAGE_TIMEOUT_MS);
    const bool release = dashSoftEngageRelease(dashSoftEngage, legacySoftEngageSent,
                                               apRestoreState.steerSeen,
                                               apRestoreState.steerValidity,
                                               apRestoreState.steerAngleX10,
                                               stable, timeout, SOFT_ENGAGE_ANGLE_THRESH_X10);
    if (stable && !release)
    {
        legacyFsdLastAllowed = false;
        legacyFsdLastBlockedMs = nowMs;
        return false; // Soft Engage hold: wheel off-centre, within timeout window
    }
    if (stable)
        legacySoftEngageSent = true; // latch: angle ignored for rest of episode
    legacyFsdLastAllowed = stable;
    if (!stable)
        legacyFsdLastBlockedMs = nowMs;
    return stable;
}

static FsdGateBlockReason dashCurrentGateBlockReason()
{
    if (!canActive)
        return FsdGateBlockReason::CanActive;
    if (!dashOtaGuardAllowInjection())
        return FsdGateBlockReason::Ota;
    if (!dashApInjectionAllowed())
        return FsdGateBlockReason::ApGate;
    if (!enhancedAutopilotInjectionAllowed(dashHandler && dashHandler->injectionGateOpen()))
        return FsdGateBlockReason::CompileGate;
    return FsdGateBlockReason::CheckAd;
}

static bool dashInjectionActive()
{
    // Phase 1: OTA保护 — 车辆OTA进行中时暂停注入
    if (!dashOtaGuardAllowInjection())
        return false;
    return canActive && dashApInjectionAllowed();
}

#if defined(DASH_PLUGIN_ENGINE)
// Builds the runtime context handed to DashPluginEngine.applyToFrame() /
// tickPeriodic(). Mirrors the dashboard injection gates (CAN active + OTA guard
// + AP gate + legacy FSD settle) so plugins only act when injection is allowed.
//
// IMPORTANT: this is called every CAN frame in appLoop, so it must be free of
// side effects. We do NOT call dashLegacyFsdActivationAllowed() here (it resets
// the settle timer / writes legacyFsdLastAllowed) — instead we read
// legacyFsdLastAllowed, the latched result of the last handler-path gate check.
// dashLegacyFsdActivationAllowed() is still invoked by the real handler gate
// path (via legacyFsdActivationAllowed callback) and updates the latch.
static DashPluginContext dashPluginContext()
{
    DashPluginContext ctx{};
    ctx.canActive = canActive;
    ctx.otaAllowed = dashOtaGuardAllowInjection();
    ctx.apGateAllowed = dashApInjectionAllowed() && legacyFsdLastAllowed;
    ctx.fsdMasterEnabled = canActive;
    ctx.defaultBus = CAN_BUS_DEFAULT;
    return ctx;
}

// Returns true when any enabled+compatible plugin declares a rule for an FSD
// activation frame id (1006 / 1021 / 2047). Used to suppress the built-in FSD
// injection so the plugin owns those frames. Guarded-out builds always return
// false, so the built-in path stays active (no behavior change).
static bool dashPluginOwnsFsdActivation()
{
    return dashPluginEngine.hasEnabledRuleFor(1006) ||
           dashPluginEngine.hasEnabledRuleFor(1021) ||
           dashPluginEngine.hasEnabledRuleFor(2047);
}
#else
static bool dashPluginOwnsFsdActivation()
{
    return false;
}
#endif

static bool dashApRestoreBraking()
{
    return (apRestoreState.brakeSeen && apRestoreState.brakePedalRaw == 1) ||
           (apRestoreState.chassisSeen && apRestoreState.brakeTorqueActive);
}

static bool dashApRestoreStabilityBlocked()
{
    return apRestoreState.chassisSeen &&
           (apRestoreState.anyVdcActive == 1 || apRestoreState.vdcControlActive > 0 ||
            apRestoreState.tcActive);
}

static void dashTryApAutoRestore(const CanFrame &trigger, CanDriver &driver)
{
    if (trigger.id != 0x389 || !apAutoRestore)
        return;

    unsigned long now = millis();
    if (!apRestoreState.dasAccDropMs ||
        apRestoreState.lastDropHandledMs == apRestoreState.dasAccDropMs ||
        now - apRestoreState.dasAccDropMs > 250)
        return;

    apRestoreState.lastDropHandledMs = apRestoreState.dasAccDropMs;
    if (!apRestoreState.dasSettingsSeen || now - apRestoreState.dasSettingsMs > 5000)
        return;
    if (!apRestoreState.gearSeen || apRestoreState.gearRaw != 4)
        return;
    if (dashApRestoreBraking() || dashApRestoreStabilityBlocked())
        return;
    if (apRestoreState.lastTxMs && now - apRestoreState.lastTxMs < kDashApRestoreTxCooldownMs)
        return;

    CanFrame modified{};
    modified.id = 0x293;
    modified.bus = CAN_BUS_DEFAULT;
    modified.dlc = 8;
    memcpy(modified.data, apRestoreState.dasSettingsData, 8);
    CanFrame original = modified;
    setBit(modified, 38, true);
    setBit(modified, 24, true);
    uint8_t counter = static_cast<uint8_t>(((apRestoreState.dasSettingsCounter == 0xFF ? 0 : apRestoreState.dasSettingsCounter) + 1) & 0x0F);
    modified.data[7] = static_cast<uint8_t>((modified.data[7] & 0xF0) | counter);
    modified.data[7] = dashCounterChecksumByte(modified);
    if (!framePayloadChanged(original, modified))
        return;

    bool ok = driver.send(modified);
    apRestoreState.lastTxMs = now;
    if (ok)
        lastInjectMs = now;
    dashRecordCanFrame(modified, ok ? 'T' : 'E');
    dashLog("[AP] Auto-restore " + String(ok ? "TX OK" : "TX FAIL"));
}

static void dashPostProcessFrame(const CanFrame &original, CanDriver &driver)
{
    dashTryApAutoRestore(original, driver);
    // FSD activation injection is owned by Legacy/HW3/HW4 handlers.
    // Do not post-process mux0 here; doing so can double-send on dashboard builds.
}

static bool dashCheckNagDisabled()
{
    return false;
}

static bool dashStaSsidLooksCorrupt(const String &ssid)
{
    return ssid.indexOf("\"ssid\"") >= 0 || ssid.indexOf("{\"") >= 0 ||
           ssid.indexOf("\",\"") >= 0;
}

// dashClampHw3SlewRate / dashLoadHw3SlewRate now in dash_hw3_speed.h.

static uint8_t dashClampSpeedProfileForHw(uint8_t hw, int profile)
{
    int maxProfile = hw == 2 ? 4 : 2;
    if (profile < 0)
        return 0;
    if (profile > maxProfile)
        return static_cast<uint8_t>(maxProfile);
    return static_cast<uint8_t>(profile);
}

static bool dashArgTruthy(const String &v)
{
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

// Clamp an AP settle delay (ms) parsed from /config into the valid range so a
// bad or out-of-range request can never disable the legacy FSD activation gate
// accidentally. Mirrors dashClampSpeedProfileForHw / dashClampHw3SlewRate style.
static uint32_t dashClampApDelayMs(int value)
{
    if (value < static_cast<int>(kLegacyFsdActivationSettleMinMs))
        return kLegacyFsdActivationSettleMinMs;
    if (value > static_cast<int>(kLegacyFsdActivationSettleMaxMs))
        return kLegacyFsdActivationSettleMaxMs;
    return static_cast<uint32_t>(value);
}

static uint8_t dashClampSpeedCustomPct(int v)
{
    if (v < 0)
        return 0;
    if (v > 50)
        return 50;
    return static_cast<uint8_t>(v);
}

static const char *dashHwModeName(uint8_t mode)
{
    switch (mode)
    {
    case 0:
        return "legacy";
    case 1:
        return "HW3";
    case 2:
        return "HW4";
    case 3:
        return "Auto";
    default:
        return "unknown";
    }
}

static int dashHwModeFromName(String mode)
{
    mode.toLowerCase();
    if (mode == "auto")
        return 3;
    if (mode == "legacy")
        return 0;
    if (mode == "hw3" || mode == "hw3.0")
        return 1;
    if (mode == "hw4" || mode == "hw4.0")
        return 2;
    return -1;
}

static const char *dashDriveProfileName(uint8_t profile)
{
    static const char *const names[] = {"Auto", "Sloth", "Chill", "Normal", "Hurry", "MAX"};
    return profile < 6 ? names[profile] : "Normal";
}

static int dashDriveProfileFromName(String profile)
{
    profile.toLowerCase();
    if (profile == "auto")
        return 0;
    if (profile == "sloth")
        return 1;
    if (profile == "chill")
        return 2;
    if (profile == "normal")
        return 3;
    if (profile == "hurry")
        return 4;
    if (profile == "max")
        return 5;
    return -1;
}

static uint8_t dashSpeedProfileForDrive(uint8_t profile)
{
    switch (profile)
    {
    case 1: // Sloth
    case 2: // Chill
        return 0;
    case 4: // Hurry
        return 2;
    case 5: // MAX
        return 4;
    case 3: // Normal
    default:
        return 1;
    }
}

static const char *dashSpeedStrategyName(uint8_t strategy)
{
    switch (strategy)
    {
    case 0:
        return "fixed";
    case 1:
        return "auto";
    case 2:
        return "custom";
    default:
        return "auto";
    }
}

static int dashSpeedStrategyFromName(String strategy)
{
    strategy.toLowerCase();
    if (strategy == "fixed")
        return 0;
    if (strategy == "auto")
        return 1;
    if (strategy == "custom")
        return 2;
    return -1;
}

static const char *dashLightingFrequencyName(uint8_t frequency)
{
    switch (frequency)
    {
    case 0:
        return "slow";
    case 1:
        return "medium";
    case 2:
        return "fast";
    default:
        return "medium";
    }
}

static int dashLightingFrequencyFromName(String frequency)
{
    frequency.toLowerCase();
    if (frequency == "slow")
        return 0;
    if (frequency == "medium")
        return 1;
    if (frequency == "fast")
        return 2;
    return -1;
}

static const char *dashRearFogStrategyName(uint8_t strategy)
{
    switch (strategy)
    {
    case 0:
        return "off";
    case 1:
        return "strobe";
    case 2:
        return "continuous";
    default:
        return "off";
    }
}

static int dashRearFogStrategyFromName(String strategy)
{
    strategy.toLowerCase();
    if (strategy == "off")
        return 0;
    if (strategy == "strobe")
        return 1;
    if (strategy == "continuous")
        return 2;
    return -1;
}

static void dashApplySpeedProfileState()
{
    if (!dashHandler)
        return;
    dashHandler->speedProfileAuto = dashSpeedProfileAuto;
    if (!dashSpeedProfileAuto)
        dashHandler->speedProfile = dashClampSpeedProfileForHw(hwMode, dashManualSpeedProfile);
}

// HW3 mux-2 codec + slew limiter live in include/dash_hw3_speed.h so they
// can be shared between HW3Handler (in handlers.h, parsed first) and the
// dashboard HW3 send path below.

static void dashApplyRuntimeState()
{
    forceActivateRuntime = canActive && forceActivate;
    emergencyVehicleDetectionRuntime = false;
    isaSpeedChimeSuppressRuntime = false;
    enhancedAutopilotRuntime = false;
    nagKillerRuntime = canActive && dashDefenseEnabled;

    if (dashHandler)
    {
        dashHandler->checkAD = dashCheckADEnabled;
        dashHandler->gateBlockReason = dashCurrentGateBlockReason;
        dashHandler->legacyFsdActivationAllowed = dashLegacyFsdActivationAllowed;
        dashHandler->pluginOwnsFsdActivation = dashPluginOwnsFsdActivation;
        dashHandler->checkNag = dashCheckNagDisabled;
        dashHandler->bionicSteering = dashBionicSteering;
        dashHandler->isaChimeSuppress = nvsIsaChimeSuppress;
        dashHandler->isaOverride = nvsIsaOverride;
        dashHandler->banShieldEnable = nvsBanShieldEnable;
        dashHandler->removeVisionSpeedLimit = nvsRemoveVisionSpeedLimit;
        dashHandler->overrideSpeedLimit = nvsOverrideSpeedLimit;
        dashHandler->legacyOffset = nvsLegacyOffset;
        dashHandler->tlsscBypass = nvsTlsscBypass;
        dashHandler->emergencyVehicleDetection = nvsEmergencyVehicleDetection;
        dashHandler->hw4OffsetRaw = nvsHw4OffsetRaw;
        dashHandler->autoModeEnabled = nvsAutoModeEnabled;
        dashHandler->legacyFsdDiag.policy = dashLegacyFsdPolicy;
        dashHandler->legacyFsdDiag.mux1Enable = dashLegacyFsdMux1Enable;
        dashHandler->legacyFsdDiag.profileWriteEnable = dashLegacyFsdProfileWriteEnable;
        dashHandler->legacyFsdDiag.visionLimitClearEnable = dashLegacyFsdVisionLimitClearEnable;
        dashApplySpeedProfileState();
        if (!canActive)
        {
            dashHandler->ADEnabled = false;
            dashHandler->APActive = false;
        }
    }

#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed();
#endif
}

// Store config
static void dashSavePrefs()
{
    prefs.begin(PREFS_NS, false);
    prefs.putUChar("hw", hwMode);
    prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    prefs.putBool("can", canActive);
    prefs.putBool("force_act", forceActivate);
    prefs.putBool("boot_can", bootCanActive);
    prefs.putBool("ap_gate", apInjectionGate);
    prefs.putBool("ap_rst", apAutoRestore);
    prefs.putUInt("ap_dly", legacyFsdRequiredStableMs);
    prefs.putBool("sp_auto", dashSpeedProfileAuto);
    prefs.putUChar("sp_sel", dashManualSpeedProfile);
    prefs.putUChar("drv_prof", dashDriveProfile);
    prefs.putUChar("spd_str", dashSpeedStrategy);
    prefs.putUChar("offsetMode", offsetMode);     // 0=fixed, 1=auto, 2=custom
    prefs.putUChar("manualPct", manualOffsetPct); // 0-50% for fixed mode
    prefs.putUChar("cp0", customPct[0]);          // Zone ≤50 km/h  (HTTP: cp1)
    prefs.putUChar("cp1", customPct[1]);          // Zone ≤70 km/h  (HTTP: cp2)
    prefs.putUChar("cp2", customPct[2]);          // Zone ≤100 km/h (HTTP: cp3)
    prefs.putUChar("cp3", customPct[3]);          // Zone >100 km/h (HTTP: cp4)
    prefs.putBool("lt_en", dashLightingEnabled);
    prefs.putUChar("lt_cnt", dashLightingCount);
    prefs.putUChar("lt_freq", dashLightingFrequency);
    prefs.putUChar("lt_fog", dashRearFogStrategy);
    prefs.putBool("def_en", dashDefenseEnabled);
    prefs.putBool("def_bio", dashBionicSteering);
    prefs.putBool("def_ntt", dashNagTorqueTamper);
    prefs.putBool("def_se", dashSoftEngage);
    prefs.putBool("def_nd", dashSpeedNoDisturb);
    prefs.putBool("def_dv", dashDndVolume);
    prefs.putBool("def_ds", dashDndSpeed);
    prefs.putBool("def_apeap", dashApEapCompatible);
    prefs.putUChar("lfsd_pol", dashLegacyFsdPolicy == LegacyFsdPolicy::TeslaParity ? 2 : (dashLegacyFsdPolicy == LegacyFsdPolicy::Experimental ? 1 : 0));
    prefs.putBool("lfsd_m1", dashLegacyFsdMux1Enable);
    prefs.putBool("lfsd_prof", dashLegacyFsdProfileWriteEnable);
    prefs.putBool("lfsd_vis", dashLegacyFsdVisionLimitClearEnable);
    prefs.putBool("eprn", dashHandler ? (bool)dashHandler->enablePrint : true);
    prefs.putBool("h3_slw", hw3OffsetSlew);
    prefs.putUChar("h3_srt", hw3SlewRate);
    // HW3 custom speed-limit boost
    prefs.putBool("h3_cust", hw3CustomSpeed);
    prefs.putBool("h3_hse", hw3HighSpeedEnable);
    prefs.putUChar("h3_enc", hw3WireEncoding);
    char k[8];
    for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
    {
        snprintf(k, sizeof(k), "h3_ct%u", (unsigned)i);
        prefs.putUChar(k, hw3CustomTarget[i]);
    }
    for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
    {
        snprintf(k, sizeof(k), "h3_ht%u", (unsigned)i);
        prefs.putUChar(k, hw3HighSpeedTarget[i]);
    }
    // Legacy MPP custom speed-limit override
    prefs.putBool("lg_mpp_en", legacyMppOverride);
    prefs.putBool("lg_mppc_en", legacyMppCustomEnable);
    prefs.putBool("lg_mpph_en", legacyMppHighSpeedEnable);
    for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
    {
        snprintf(k, sizeof(k), "lg_ct%u", (unsigned)i);
        prefs.putUChar(k, legacyMppCustomTarget[i]);
    }
    for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
    {
        snprintf(k, sizeof(k), "lg_ht%u", (unsigned)i);
        prefs.putUChar(k, legacyMppHighSpeedTarget[i]);
    }
    // --- FSD runtime switches (obfuscated NVS keys) ---
    if (dashHandler)
    {
        prefs.putBool("fa", (bool)dashHandler->autoModeEnabled);
        prefs.putBool("fb", (bool)dashHandler->tlsscBypass);
        prefs.putBool("fc", (bool)dashHandler->emergencyVehicleDetection);
        prefs.putBool("fd", (bool)dashHandler->isaChimeSuppress);
        prefs.putBool("fj", (bool)dashHandler->isaOverride);
        prefs.putUChar("fe", (uint8_t)dashHandler->hw4OffsetRaw);
        prefs.putBool("ff", (bool)dashHandler->banShieldEnable);
        prefs.putUChar("fg", (uint8_t)((int)dashHandler->legacyOffset + 30));
        prefs.putBool("fh", (bool)dashHandler->removeVisionSpeedLimit);
        prefs.putBool("fi", (bool)dashHandler->overrideSpeedLimit);
    }
    prefs.end();
}

static void dashSetCanActive(bool active, const char *reason = nullptr)
{
    bool changed = (canActive != active) || (forceActivate != active);
    canActive = active;
    forceActivate = active;
    dashApplyRuntimeState();
    dashSavePrefs();
    if (changed)
    {
        String msg = String("[CFG] FSD master switch ") + (active ? "ON" : "OFF");
        if (reason && *reason)
            msg += String(" via ") + reason;
        dashLog(msg);
    }
}

[[maybe_unused]] static void dashToggleCanActive(const char *reason = nullptr)
{
    dashSetCanActive(!canActive, reason);
}

static bool dashApPasswordLengthValid(size_t len)
{
    return len >= kDashMinApPassLen && len <= kDashMaxPassLen;
}

static bool dashApConfigValid(const char *ssid, const char *pass)
{
    size_t ssidLen = strlen(ssid);
    size_t passLen = strlen(pass);
    return ssidLen > 0 && ssidLen <= kDashMaxSsidLen && dashApPasswordLengthValid(passLen);
}

static void dashUseDefaultApConfig()
{
    strlcpy(apSSID, DASH_SSID, sizeof(apSSID));
    strlcpy(apPass, DASH_PASS, sizeof(apPass));
    apHidden = false;
    apRuntimeChannel = kDashApChannel;
}

static uint8_t dashConfiguredApChannel()
{
    return kDashApChannel;
}

static bool dashStaConfigLengthValid(const String &ssid, const String &pass)
{
    return ssid.length() <= kDashMaxSsidLen && pass.length() <= kDashMaxPassLen;
}

static void dashClearLegacyOptionPrefs()
{
    static const char *const keys[] = {
        "fAD",
        "f_AD",
        "f_nag",
        "f_sum",
        "f_isa",
        "f_evd",
        "f_h4o",
        "sp",
        "sp_lock",
    };

    bool removed = false;
    for (const char *key : keys)
    {
        if (!prefs.isKey(key))
            continue;
        prefs.remove(key);
        removed = true;
    }

    if (removed)
        dashLog("[BOOT] Cleared legacy dashboard prefs from NVS");
}

static void dashLoadPrefs()
{
    prefs.begin(PREFS_NS, false);
    dashClearLegacyOptionPrefs();
    bool hasStoredHw = prefs.isKey("hw");
    uint8_t storedHw = prefs.getUChar("hw", DASH_DEFAULT_HW);
    uint8_t storedDefaultHw = prefs.getUChar("hw_def", kDashUnsetU8);
    bool migratedHw = false;

    hwMode = storedHw <= 3 ? storedHw : DASH_DEFAULT_HW;
    if (!hasStoredHw || storedHw > 3)
        migratedHw = true;

    // If the stored selection only mirrors the old firmware default, follow the
    // new build default after reflashing instead of staying pinned to stale NVS.
    if (storedDefaultHw <= 2 && storedDefaultHw != DASH_DEFAULT_HW && hwMode == storedDefaultHw)
    {
        hwMode = DASH_DEFAULT_HW;
        migratedHw = true;
    }

    if (migratedHw)
        prefs.putUChar("hw", hwMode);
    if (storedDefaultHw != DASH_DEFAULT_HW)
        prefs.putUChar("hw_def", DASH_DEFAULT_HW);
    bootCanActive = prefs.getBool("boot_can", prefs.getBool("can", kDashInjectionDefaultEnabled));
    canActive = bootCanActive;
    forceActivate = canActive;
    if (prefs.getBool("can", canActive) != canActive)
        prefs.putBool("can", canActive);
    if (prefs.getBool("force_act", canActive) != forceActivate)
        prefs.putBool("force_act", forceActivate);
    if (prefs.getBool("boot_can", bootCanActive) != bootCanActive)
        prefs.putBool("boot_can", bootCanActive);
    // 默认 false：复刻 2.5.2 真车固件行为（apInjectionGate=false 注入无条件放行）。
    apInjectionGate = prefs.getBool("ap_gate", DASH_AP_GATE_DEFAULT);
    apAutoRestore = prefs.getBool("ap_rst", false);
    legacyFsdRequiredStableMs = dashClampApDelayMs(static_cast<int>(prefs.getUInt("ap_dly", kLegacyFsdActivationSettleDefaultMs)));
    dashSpeedProfileAuto = prefs.getBool("sp_auto", true);
    dashManualSpeedProfile = dashClampSpeedProfileForHw(hwMode, prefs.getUChar("sp_sel", 1));
    dashDriveProfile = prefs.getUChar("drv_prof", dashSpeedProfileAuto ? 0 : 3);
    if (dashDriveProfile > 5)
        dashDriveProfile = 0;
    dashSpeedStrategy = prefs.getUChar("spd_str", dashSpeedProfileAuto ? 1 : 0);
    if (dashSpeedStrategy > 2)
        dashSpeedStrategy = 1;
    offsetMode = prefs.getUChar("offsetMode", dashSpeedStrategy); // see dash_hw3_speed.h mapping
    if (offsetMode > 2)
        offsetMode = 1;
    dashSpeedStrategy = offsetMode;
    manualOffsetPct = dashClampSpeedCustomPct(prefs.getUChar("manualPct", manualOffsetPct));
    customPct[0] = dashClampSpeedCustomPct(prefs.getUChar("cp0", customPct[0])); // Zone ≤50  (HTTP: cp1)
    customPct[1] = dashClampSpeedCustomPct(prefs.getUChar("cp1", customPct[1])); // Zone ≤70  (HTTP: cp2)
    customPct[2] = dashClampSpeedCustomPct(prefs.getUChar("cp2", customPct[2])); // Zone ≤100 (HTTP: cp3)
    customPct[3] = dashClampSpeedCustomPct(prefs.getUChar("cp3", customPct[3])); // Zone >100 (HTTP: cp4)
    dashSyncLegacyShims();
    dashLightingEnabled = prefs.getBool("lt_en", false);
    dashLightingCount = prefs.getUChar("lt_cnt", 3);
    if (!(dashLightingCount == 3 || dashLightingCount == 5 || dashLightingCount == 7 || dashLightingCount == 10))
        dashLightingCount = 3;
    dashLightingFrequency = prefs.getUChar("lt_freq", 1);
    if (dashLightingFrequency > 2)
        dashLightingFrequency = 1;
    dashRearFogStrategy = prefs.getUChar("lt_fog", 0);
    if (dashRearFogStrategy > 2)
        dashRearFogStrategy = 0;
    dashDefenseEnabled = prefs.getBool("def_en", false);
    dashBionicSteering = prefs.getBool("def_bio", false);
    dashNagTorqueTamper = prefs.getBool("def_ntt", false);
    nagTorqueTamperRuntime = dashNagTorqueTamper; // boot-sync opt-in to NagHandler
    dashSoftEngage = prefs.getBool("def_se", kSoftEngageDefaultEnabled);
    dashSpeedNoDisturb = prefs.getBool("def_nd", false);
    dashDndVolume = prefs.getBool("def_dv", false);
    dashDndSpeed = prefs.getBool("def_ds", false);
    dashApEapCompatible = prefs.getBool("def_apeap", true);
    {
        uint8_t lfsdPolicy = prefs.getUChar("lfsd_pol", 0);
        dashLegacyFsdPolicy = lfsdPolicy == 2 ? LegacyFsdPolicy::TeslaParity : (lfsdPolicy == 1 ? LegacyFsdPolicy::Experimental : LegacyFsdPolicy::Stable);
    }
    dashLegacyFsdMux1Enable = prefs.getBool("lfsd_m1", false);
    dashLegacyFsdProfileWriteEnable = prefs.getBool("lfsd_prof", false);
    dashLegacyFsdVisionLimitClearEnable = prefs.getBool("lfsd_vis", false);
    hw3OffsetSlew = prefs.getBool("h3_slw", false);
    hw3SlewRate = dashLoadHw3SlewRate(prefs.getUChar("h3_srt", kHw3SlewRateDefault));
    // HW3 custom speed-limit boost
    hw3CustomSpeed = prefs.getBool("h3_cust", false);
    hw3HighSpeedEnable = prefs.getBool("h3_hse", false);
    {
        uint8_t enc = prefs.getUChar("h3_enc", kHw3WireEncDefault);
        hw3WireEncoding = (enc == kHw3WireEncKph5) ? kHw3WireEncKph5 : kHw3WireEncPct4;
    }
    {
        char k[8];
        static const uint8_t defCt[kHw3CustomTargetCount] = {45, 60, 75, 90, 105};
        static const uint8_t defHs[kHw3HighSpeedBucketCount] = {90, 110, 130};
        for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
        {
            snprintf(k, sizeof(k), "h3_ct%u", (unsigned)i);
            hw3CustomTarget[i] = dashClampHw3CustomTargetForBucket(i, prefs.getUChar(k, defCt[i]));
        }
        for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
        {
            snprintf(k, sizeof(k), "h3_ht%u", (unsigned)i);
            hw3HighSpeedTarget[i] = dashClampHw3HighSpeedTargetForBucket(i,
                                                                         prefs.getUChar(k, defHs[i]));
        }
    }
    dashSyncLegacyShims();
    // Legacy MPP custom speed-limit override
    legacyMppOverride = prefs.getBool("lg_mpp_en", false);
    legacyMppCustomEnable = prefs.getBool("lg_mppc_en", false);
    legacyMppHighSpeedEnable = prefs.getBool("lg_mpph_en", false);
    {
        char k[8];
        static const uint8_t defLgCt[kLegacyMppCustomTargetCount] = {45, 60, 75, 90, 105};
        static const uint8_t defLgHt[kLegacyMppHighSpeedBucketCount] = {90, 110, 130};
        for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
        {
            snprintf(k, sizeof(k), "lg_ct%u", (unsigned)i);
            legacyMppCustomTarget[i] = dashClampLegacyMppCustomTargetForBucket(i, prefs.getUChar(k, defLgCt[i]));
        }
        for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
        {
            snprintf(k, sizeof(k), "lg_ht%u", (unsigned)i);
            legacyMppHighSpeedTarget[i] = dashClampLegacyMppHighSpeedTargetForBucket(i, prefs.getUChar(k, defLgHt[i]));
        }
    }
    bool ep = prefs.getBool("eprn", true);
    // --- FSD runtime switches (staged, applied after handlerPool init) ---
    nvsAutoModeEnabled = prefs.getBool("fa", false);
    nvsTlsscBypass = prefs.getBool("fb", false);
    nvsEmergencyVehicleDetection = prefs.getBool("fc", true);
    nvsIsaChimeSuppress = prefs.getBool("fd", false);
    nvsIsaOverride = prefs.getBool("fj", false);
    nvsHw4OffsetRaw = prefs.getUChar("fe", 0);
    nvsBanShieldEnable = prefs.getBool("ff", false);
    {
        uint8_t raw = prefs.getUChar("fg", 30);
        nvsLegacyOffset = (int)raw - 30;
    }
    nvsRemoveVisionSpeedLimit = prefs.getBool("fh", true);
    nvsOverrideSpeedLimit = prefs.getBool("fi", false);

    dashApplyRuntimeState();
    if (dashHandler)
        dashHandler->enablePrint = ep;
    // Load WiFi AP overrides (hotspot name/password)
    String apSsidPref = prefs.isKey("ap_ssid") ? prefs.getString("ap_ssid", "") : "";
    String apPassPref = prefs.isKey("ap_pass") ? prefs.getString("ap_pass", "") : "";
    bool hasApOverride = apSsidPref.length() > 0 || apPassPref.length() > 0 || prefs.isKey("ap_hidden");
    bool invalidApOverride = apSsidPref.length() > kDashMaxSsidLen ||
                             (apPassPref.length() > 0 && !dashApPasswordLengthValid(apPassPref.length()));
    if (apSsidPref.length() > 0)
        strlcpy(apSSID, apSsidPref.c_str(), sizeof(apSSID));
    else
        strlcpy(apSSID, DASH_SSID, sizeof(apSSID));
    if (apPassPref.length() > 0)
        strlcpy(apPass, apPassPref.c_str(), sizeof(apPass));
    else
        strlcpy(apPass, DASH_PASS, sizeof(apPass));
    apHidden = prefs.getBool("ap_hidden", false);
    if (invalidApOverride || !dashApConfigValid(apSSID, apPass))
    {
        if (hasApOverride)
        {
            prefs.remove("ap_ssid");
            prefs.remove("ap_pass");
            prefs.remove("ap_hidden");
            dashLog("[WIFI] Invalid saved AP config ignored");
        }
        dashUseDefaultApConfig();
    }
    apRuntimeChannel = dashConfiguredApChannel();

    // Load WiFi STA networks (multi-SSID slot array)
    wifiNetworkCount = 0;
    for (uint8_t i = 0; i < kDashMaxWifiNetworks; i++)
        dashClearWifiNetwork(wifiNetworks[i]);

    uint8_t storedCount = prefs.getUChar("wn_cnt", 0);
    if (storedCount > kDashMaxWifiNetworks)
        storedCount = kDashMaxWifiNetworks;

    for (uint8_t i = 0; i < storedCount; i++)
    {
        DashWifiNetwork &n = wifiNetworks[wifiNetworkCount];
        String s = prefs.getString(dashWifiKey(i, "s").c_str(), "");
        String p = prefs.getString(dashWifiKey(i, "p").c_str(), "");
        if (!dashStaConfigLengthValid(s, p) || dashStaSsidLooksCorrupt(s) || s.length() == 0)
            continue;
        strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
        strlcpy(n.pass, p.c_str(), sizeof(n.pass));
        n.useStatic = prefs.getBool(dashWifiKey(i, "t").c_str(), false);
        if (n.useStatic)
        {
            String ip = prefs.getString(dashWifiKey(i, "i").c_str(), "0.0.0.0");
            String gw = prefs.getString(dashWifiKey(i, "g").c_str(), "0.0.0.0");
            String mk = prefs.getString(dashWifiKey(i, "m").c_str(), "255.255.255.0");
            String dn = prefs.getString(dashWifiKey(i, "d").c_str(), "0.0.0.0");
            strlcpy(n.ip, ip.c_str(), sizeof(n.ip));
            strlcpy(n.gw, gw.c_str(), sizeof(n.gw));
            strlcpy(n.mask, mk.c_str(), sizeof(n.mask));
            strlcpy(n.dns, dn.c_str(), sizeof(n.dns));
        }
        wifiNetworkCount++;
    }

    // One-shot migration from legacy single-SSID keys
    if (wifiNetworkCount == 0 && prefs.isKey("wifi_ssid"))
    {
        String s = prefs.getString("wifi_ssid", "");
        String p = prefs.getString("wifi_pass", "");
        if (dashStaConfigLengthValid(s, p) && !dashStaSsidLooksCorrupt(s) && s.length() > 0)
        {
            DashWifiNetwork &n = wifiNetworks[0];
            strlcpy(n.ssid, s.c_str(), sizeof(n.ssid));
            strlcpy(n.pass, p.c_str(), sizeof(n.pass));
            n.useStatic = prefs.getBool("wifi_static", false);
            if (n.useStatic)
            {
                strlcpy(n.ip, prefs.getString("wifi_ip", "0.0.0.0").c_str(), sizeof(n.ip));
                strlcpy(n.gw, prefs.getString("wifi_gw", "0.0.0.0").c_str(), sizeof(n.gw));
                strlcpy(n.mask, prefs.getString("wifi_mask", "255.255.255.0").c_str(), sizeof(n.mask));
                strlcpy(n.dns, prefs.getString("wifi_dns", "0.0.0.0").c_str(), sizeof(n.dns));
            }
            wifiNetworkCount = 1;
            prefs.putUChar("wn_cnt", 1);
            prefs.putString(dashWifiKey(0, "s").c_str(), s);
            prefs.putString(dashWifiKey(0, "p").c_str(), p);
            prefs.putBool(dashWifiKey(0, "t").c_str(), n.useStatic);
            if (n.useStatic)
            {
                prefs.putString(dashWifiKey(0, "i").c_str(), String(n.ip));
                prefs.putString(dashWifiKey(0, "g").c_str(), String(n.gw));
                prefs.putString(dashWifiKey(0, "m").c_str(), String(n.mask));
                prefs.putString(dashWifiKey(0, "d").c_str(), String(n.dns));
            }
            dashLog("[WIFI] Migrated legacy STA config to slot 0");
        }
        prefs.remove("wifi_ssid");
        prefs.remove("wifi_pass");
        prefs.remove("wifi_static");
        prefs.remove("wifi_ip");
        prefs.remove("wifi_gw");
        prefs.remove("wifi_mask");
        prefs.remove("wifi_dns");
    }

    // Seed staSSID/staPass with the preferred slot (last network that
    // successfully connected) if it's still valid, otherwise fall back to
    // slot 0. Connecting to the last-known-good network first is much faster
    // than always rotating from slot 0 across reboots.
    if (wifiNetworkCount > 0)
    {
        uint8_t preferred = prefs.getUChar("wn_pref", 0);
        if (preferred >= wifiNetworkCount)
            preferred = 0;
        const DashWifiNetwork &n = wifiNetworks[preferred];
        strlcpy(staSSID, n.ssid, sizeof(staSSID));
        strlcpy(staPass, n.pass, sizeof(staPass));
        staStaticIP = n.useStatic;
        if (n.useStatic)
        {
            staIP.fromString(n.ip);
            staGW.fromString(n.gw);
            staMask.fromString(n.mask);
            staDNS.fromString(n.dns);
        }
        wifiActiveSlot = static_cast<int8_t>(preferred);
        wifiNextRotateSlot = preferred;
    }
    else
    {
        staSSID[0] = 0;
        staPass[0] = 0;
        staStaticIP = false;
        wifiActiveSlot = -1;
        wifiNextRotateSlot = 0;
    }

    updateBetaChannel = prefs.getBool("update_beta", false);
    autoUpdateEnabled = prefs.getBool("auto_upd", false);
    // Phase 1: 功耗管理 NVS 加载
    autoShutdownEnabled = prefs.getBool(NVS_KEY_AUTO_SHUTDOWN, false);
    wifiAutoOffEnabled = prefs.getBool(NVS_KEY_WIFI_AUTO_OFF, false);
    prefs.end();

    if (migratedHw)
        dashLog("[BOOT] HW default synced to " + String(hwMode == 0 ? "LEGACY" : hwMode == 1 ? "HW3"
                                                                                             : "HW4"));
    dashLog("[BOOT] Prefs loaded HW=" + String(hwMode));
    dashLog("[BOOT] canActive=" + String(canActive ? "YES" : "NO"));
}

// MCP2515-only: fine-grained filter register reload on HW mode switch.
// Other builds use dashDriver->setFilters() in dashSwapHandler instead.
static void dashApplyFilters()
{
#if defined(DRIVER_ESP32_EXT_MCP2515)
    if (!dashMcp)
        return;
    dashMcp->setConfigMode();
    if (hwMode == 0)
    {
        // Legacy needs more than six observer IDs (69/280/390/OTA plus injection IDs).
        // Use RXB0 as a broad observer bucket; LegacyHandler ignores unrelated frames.
        dashMcp->setFilterMask(MCP2515::MASK0, false, 0x000);
        dashMcp->setFilter(MCP2515::RXF0, false, 0);
        dashMcp->setFilter(MCP2515::RXF1, false, 0);
        dashMcp->setFilterMask(MCP2515::MASK1, false, 0x7FF);
        dashMcp->setFilter(MCP2515::RXF2, false, 760);
        dashMcp->setFilter(MCP2515::RXF3, false, 921);
        dashMcp->setFilter(MCP2515::RXF4, false, 1006);
        dashMcp->setFilter(MCP2515::RXF5, false, 1080);
    }
    else if (hwMode == 2)
    {
        dashMcp->setFilterMask(MCP2515::MASK0, false, 0x7FF);
        dashMcp->setFilter(MCP2515::RXF0, false, 921);
        dashMcp->setFilter(MCP2515::RXF1, false, 1021);
        dashMcp->setFilterMask(MCP2515::MASK1, false, 0x7FF);
        dashMcp->setFilter(MCP2515::RXF2, false, 1016);
        dashMcp->setFilter(MCP2515::RXF3, false, 280);
        dashMcp->setFilter(MCP2515::RXF4, false, CAN_ID_OTA_STATUS);
        dashMcp->setFilter(MCP2515::RXF5, false, 921);
    }
    else
    {
        dashMcp->setFilterMask(MCP2515::MASK0, false, 0x7FF);
        dashMcp->setFilter(MCP2515::RXF0, false, 1016);
        dashMcp->setFilter(MCP2515::RXF1, false, 1021);
        dashMcp->setFilterMask(MCP2515::MASK1, false, 0x7FF);
        dashMcp->setFilter(MCP2515::RXF2, false, 1016);
        dashMcp->setFilter(MCP2515::RXF3, false, 280);
        dashMcp->setFilter(MCP2515::RXF4, false, CAN_ID_OTA_STATUS);
        dashMcp->setFilter(MCP2515::RXF5, false, 1021);
    }
    dashMcp->setNormalMode();
    dashLog("[CFG] Filters set for " + String(hwMode == 0 ? "LEGACY" : hwMode == 1 ? "HW3"
                                                                                   : "HW4"));
#endif
}

// Bus-off recovery (MCP2515 only — TWAI driver handles its own bus-off internally)
#if defined(DRIVER_ESP32_EXT_MCP2515)
static unsigned long lastEflgCheckMs = 0;
static void dashCheckBusHealth()
{
    if (!dashMcp)
        return;
    if (millis() - lastEflgCheckMs < 5000)
        return;
    lastEflgCheckMs = millis();
    uint8_t eflg = dashMcp->getErrorFlags();
    mcpEflg = eflg;
    if (eflg & 0x20)
    {
        dashLog("[ERR] MCP2515 BUS-OFF -- recovering");
        dashMcp->reset();
        delay(10);
        dashMcp->setBitrate(CAN_500KBPS, MCP_CRYSTAL_FREQ);
        dashApplyFilters();
        dashLog("[OK] MCP2515 recovered");
    }
}
#else
static void dashCheckBusHealth()
{
}
#endif
static WebServer server(80);

#ifdef DRIVER_T2CAN_DUAL
// CAN2 (bus B / MCP2515, X197 pin 9/10) traffic counters — defined in main.cpp.
uint32_t t2canBus2RxCount(void);
uint32_t t2canBus2TxCount(void);
uint32_t t2canBus2TxErrCount(void);
uint8_t t2canBus2Eflg(void);
const DashTxEvidence &t2canServiceTxEvidence(void);
uint8_t t2canServiceLastCommand(void);
uint8_t t2canServiceBurstRemaining(void);
bool t2canGetServiceMode(void);
#endif

static bool dashArgUIntInRange(const char *name, uint8_t minValue, uint8_t maxValue, uint8_t &out)
{
    if (!server.hasArg(name))
        return false;
    String value = server.arg(name);
    if (value.length() == 0)
        return false;
    uint16_t parsed = 0;
    for (unsigned int i = 0; i < value.length(); i++)
    {
        char c = value.charAt(i);
        if (c < '0' || c > '9')
            return false;
        parsed = static_cast<uint16_t>(parsed * 10 + (c - '0'));
        if (parsed > maxValue)
            return false;
    }
    if (parsed < minValue)
        return false;
    out = static_cast<uint8_t>(parsed);
    return true;
}

#include "web/dash_gateway.h"

static void handleRoot()
{
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
#ifdef ESP_PLATFORM
    server.sendRaw(200, "text/html",
                   reinterpret_cast<const char *>(DASH_HTML_GZ),
                   DASH_HTML_GZ_LEN);
#else
    server.send_P(200, "text/html", reinterpret_cast<const char *>(DASH_HTML_GZ), DASH_HTML_GZ_LEN);
#endif
}

static void appendHexBytesJson(String &j, const uint8_t data[8])
{
    static const char *hex = "0123456789ABCDEF";
    j += "\"";
    for (uint8_t i = 0; i < 8; ++i)
    {
        if (i)
            j += " ";
        uint8_t b = data[i];
        j += hex[(b >> 4) & 0x0F];
        j += hex[b & 0x0F];
    }
    j += "\"";
}

static void appendTxEvidenceJson(String &j, const DashTxEvidence &ev, unsigned long now)
{
    j += R"JSON({"id":)JSON";
    j += ev.id;
    j += R"JSON(,"bus":)JSON";
    j += ev.bus;
    j += R"JSON(,"dlc":)JSON";
    j += ev.dlc;
    j += R"JSON(,"lastData":)JSON";
    appendHexBytesJson(j, ev.data);
    j += R"JSON(,"txOk":)JSON";
    j += ev.txOk;
    j += R"JSON(,"txErr":)JSON";
    j += ev.txErr;
    j += R"JSON(,"lastTxAgeMs":)JSON";
    j += ev.lastTxMs ? String(now - ev.lastTxMs) : String(-1);
    j += R"JSON(,"lastError":")JSON";
    j += dashTxErrorName(ev.lastError);
    j += "\"}";
}

static void appendTwaiDiagJson(String &j, unsigned long now)
{
    DashTwaiDiag d{};
#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI) && !defined(NATIVE_BUILD)
    if (dashDriver)
    {
        // The active LILYGO CAN-A driver is TWAIDriver in this build. Keep this
        // helper narrow and diagnostic-only; other driver types keep defaults.
        TWAIDriver *twai = static_cast<TWAIDriver *>(dashDriver);
        if (twai)
            d = twai->snapshotDiag();
    }
#endif
    j += R"JSON(,"twai":{"state":")JSON";
    j += dashTwaiStateName(d.state);
    j += R"JSON(","tec":)JSON";
    j += d.txErrorCounter;
    j += R"JSON(,"rec":)JSON";
    j += d.rxErrorCounter;
    j += R"JSON(,"msgsToTx":)JSON";
    j += d.msgsToTx;
    j += R"JSON(,"msgsToRx":)JSON";
    j += d.msgsToRx;
    j += R"JSON(,"txFailed":)JSON";
    j += d.txFailed;
    j += R"JSON(,"rxMissed":)JSON";
    j += d.rxMissed;
    j += R"JSON(,"rxOverrun":)JSON";
    j += d.rxOverrun;
    j += R"JSON(,"arbLost":)JSON";
    j += d.arbLost;
    j += R"JSON(,"busError":)JSON";
    j += d.busError;
    j += R"JSON(,"recoveries":)JSON";
    j += d.recoveries;
    j += R"JSON(,"lastRecoveryAgeMs":)JSON";
    j += d.lastRecoveryMs ? String(now - d.lastRecoveryMs) : String(-1);
    j += R"JSON(,"framesRead":)JSON";
    j += d.framesRead;
    j += R"JSON(,"framesAccepted":)JSON";
    j += d.framesAccepted;
    j += R"JSON(,"framesDropped":)JSON";
    j += d.framesDropped;
    j += R"JSON(,"readDrainBudgetHits":)JSON";
    j += d.readDrainBudgetHits;
    j += "}";
}

static void appendServiceDiagJson(String &j, unsigned long now)
{
#ifdef DRIVER_T2CAN_DUAL
    const DashTxEvidence &ev = t2canServiceTxEvidence();
    uint8_t cmd = t2canServiceLastCommand();
    j += R"JSON(,"serviceDiag":{"state":)JSON";
    j += t2canGetServiceMode() ? "true" : "false";
    j += R"JSON(,"lastCommand":")JSON";
    j += cmd == 1 ? "enter" : (cmd == 2 ? "exit" : "none");
    j += R"JSON(","burstFrames":4,"burstIntervalMs":10,"remaining":)JSON";
    j += t2canServiceBurstRemaining();
    j += R"JSON(,"tx":)JSON";
    appendTxEvidenceJson(j, ev, now);
    j += R"JSON(,"vehicleResponse":"unknown"})JSON";
#else
    j += R"JSON(,"serviceDiag":{"state":false,"lastCommand":"none","vehicleResponse":"unknown"})JSON";
#endif
}

static void serviceDiagJson(String &j, unsigned long now)
{
    appendServiceDiagJson(j, now);
}

static void appendWheelDndDiagJson(String &j, unsigned long now)
{
    j += R"JSON(,"wheelDndDiag":{"enabled":)JSON";
    j += dashDefenseEnabled ? "true" : "false";
    j += R"JSON(,"volume":)JSON";
    j += dashDndVolume ? "true" : "false";
    j += R"JSON(,"speed":)JSON";
    j += dashDndSpeed ? "true" : "false";
    j += R"JSON(,"native3c2Seen":)JSON";
    j += dashWheelDndCtrl.diag.native3c2Seen ? "true" : "false";
    j += R"JSON(,"native3c2AgeMs":)JSON";
    j += dashWheelDndCtrl.diag.native3c2Ms ? String(now - dashWheelDndCtrl.diag.native3c2Ms) : String(-1);
    j += R"JSON(,"baseFrameMode":")JSON";
    j += dashWheelDndCtrl.diag.baseFrameMode;
    j += R"JSON(","sequenceState":")JSON";
    j += dashWheelDndCtrl.diag.sequenceState;
    j += R"JSON(","tx":)JSON";
    appendTxEvidenceJson(j, dashWheelDndCtrl.diag.tx, now);
    j += "}";
}

static void appendCapabilitiesJson(String &j, uint8_t selectedHw, uint8_t effectiveHw)
{
    j += ",\"cap\":{\"handler\":\"";
    j += effectiveHw == 0 ? "LEGACY" : (effectiveHw == 1 ? "HW3" : "HW4");
    j += "\",\"selectedHw\":";
    j += selectedHw;
    j += ",\"effectiveHw\":";
    j += effectiveHw;
    j += ",\"legacyFsd\":\"";
    j += dashCapabilityLegacyFsd(effectiveHw);
    j += "\",\"legacySpeedOffset\":\"";
    j += effectiveHw == 0 ? "supported" : "unsupported_on_legacy";
    j += "\",\"serviceMode\":\"can_b_direct\",\"wheelDnd\":\"can_b_direct\",\"fogLight\":\"can_b_direct\",\"stalkTest\":\"can_b_direct\",\"bionicSteering\":\"";
    j += dashCapabilityBionicSteering(effectiveHw);
    j += "\",\"banShield\":\"";
    j += dashCapabilityBanShield(effectiveHw);
    j += "\",\"legacyMppCustom\":\"";
    j += dashCapabilityLegacyMppCustom(effectiveHw);
    j += "\",\"gearAssist\":\"not_implemented\"}";
}

static void appendFsdMuxDiagJson(String &j, const char *name, const FsdMuxDiag &mux, bool enabled, unsigned long now)
{
    j += ",\"";
    j += name;
    j += "\":{\"enabled\":";
    j += enabled ? "true" : "false";
    j += ",\"rx\":";
    j += mux.rx;
    j += ",\"tx\":";
    j += mux.tx;
    j += ",\"err\":";
    j += mux.err;
    j += ",\"lastRxAgeMs\":";
    j += mux.lastRxMs ? String(now - mux.lastRxMs) : String(-1);
    j += ",\"lastTxAgeMs\":";
    j += mux.lastTxMs ? String(now - mux.lastTxMs) : String(-1);
    j += ",\"lastSkip\":\"";
    j += fsdSkipReasonName(mux.lastSkip);
    j += R"JSON(","bus":")JSON";
    j += fsdDiagBusName(mux.bus);
    j += R"JSON(","driver":")JSON";
    j += fsdDiagDriverName(mux.driver);
    j += "\",\"before\":";
    appendHexBytesJson(j, mux.before);
    j += ",\"after\":";
    appendHexBytesJson(j, mux.after);
    j += "}";
}

// Human-readable summary of the legacy FSD injection state machine for /status.
// Mirrors the gate order in dashLegacyFsdActivationAllowed() so the dashboard UI
// can render the same 等待 AP / 稳定计时中 / 正在注入 / 已阻断 bucket the firmware uses.
static const char *dashApInjectionStateName()
{
    if (!canActive)
        return "blocked";
    if (!dashOtaGuardAllowInjection())
        return "blocked";
    if (apInjectionGate && !(dashHandler && (bool)dashHandler->APActive))
        return "waiting_ap";
    if (apInjectionGate && !legacyFsdLastAllowed)
        return "settling";
    if (legacyFsdLastAllowed || (lastInjectMs && millis() - lastInjectMs < 1000))
        return "injecting";
    return "blocked";
}

static void appendFsdDiagJson(String &j, unsigned long now)
{
    LegacyFsdDiag diag{};
    bool parked = false;
    bool apActive = false;
    bool summoning = false;
    if (dashHandler)
    {
        diag = dashHandler->legacyFsdDiag;
        parked = (bool)dashHandler->Parked;
        apActive = (bool)dashHandler->APActive;
        summoning = (bool)dashHandler->Summoning;
    }
    j += R"JSON(,"fsdDiag":{"policy":")JSON";
    j += legacyFsdPolicyName(diag.policy);
    j += "\",\"triggered\":";
    j += diag.triggered ? "true" : "false";
    j += R"JSON(,"triggerSource":")JSON";
    j += fsdTriggerSourceName(diag.triggerSource);
    j += R"JSON(","forceRuntime":)JSON";
    j += diag.forceRuntime ? "true" : "false";
    FsdHealthState health = classifyLegacyFsdHealth(diag, now, canActive);
    j += R"JSON(,"health":")JSON";
    j += fsdHealthStateName(health);
    j += R"JSON(","firstMux0RxAgeMs":)JSON";
    j += diag.firstMux0RxMs ? String(now - diag.firstMux0RxMs) : String(-1);
    j += R"JSON(,"firstMux1RxAgeMs":)JSON";
    j += diag.firstMux1RxMs ? String(now - diag.firstMux1RxMs) : String(-1);
    j += R"JSON(,"firstTxOkAgeMs":)JSON";
    j += diag.firstTxOkMs ? String(now - diag.firstTxOkMs) : String(-1);
    j += R"JSON(,"firstTxFailAgeMs":)JSON";
    j += diag.firstTxFailMs ? String(now - diag.firstTxFailMs) : String(-1);
    appendFsdMuxDiagJson(j, "mux0", diag.mux0, true, now);
    appendFsdMuxDiagJson(j, "mux1", diag.mux1, diag.policy == LegacyFsdPolicy::TeslaParity || (diag.policy == LegacyFsdPolicy::Experimental && diag.mux1Enable), now);
    appendFsdMuxDiagJson(j, "aux760", diag.aux760, true, now);
    appendFsdMuxDiagJson(j, "aux1080", diag.aux1080, true, now);
    j += ",\"gate\":{\"canActive\":";
    j += canActive ? "true" : "false";
    j += ",\"otaAllowed\":";
    j += dashOtaGuardAllowInjection() ? "true" : "false";
    j += ",\"apGateEnabled\":";
    j += apInjectionGate ? "true" : "false";
    j += ",\"apGateOpen\":";
    j += dashApInjectionAllowed() ? "true" : "false";
    j += ",\"parked\":";
    j += parked ? "true" : "false";
    j += ",\"apActive\":";
    j += apActive ? "true" : "false";
    j += ",\"apStableMs\":";
    j += (apActive && legacyFsdApActiveSinceMs > 0) ? String(now - legacyFsdApActiveSinceMs) : String(0);
    j += ",\"requiredStableMs\":";
    j += legacyFsdRequiredStableMs;
    j += ",\"legacyFsdAllowed\":";
    j += legacyFsdLastAllowed ? "true" : "false";
    j += ",\"blockedFrame\":\"0x3EE mux0\"";
    j += ",\"summoning\":";
    j += summoning ? "true" : "false";
    j += R"JSON(,"finalAllowed":)JSON";
    j += (canActive && dashOtaGuardAllowInjection() && dashApInjectionAllowed()) ? "true" : "false";
    j += R"JSON(,"lastBlockedBy":")JSON";
    j += fsdGateBlockReasonName(diag.lastBlockedBy);
    j += R"JSON(","gateReason":")JSON";
    j += fsdGateBlockReasonName(diag.lastBlockedBy);
    j += "\"}}";
}

static void handleStatus()
{
    if (canOnline && millis() - lastFrameMs > 10000)
    {
        canOnline = false;
        dashLog("[CAN] Bus OFFLINE (timeout)");
    }
    unsigned long now = millis();
    if (now - fpsLastMs >= 2000)
    {
        fps = fpsFrames * 1000.0f / max(1UL, now - fpsLastMs);
        fpsFrames = 0;
        fpsLastMs = now;
    }

    bool APActive = dashHandler ? (bool)dashHandler->APActive : false;
    bool ADEnabled = dashHandler ? (bool)dashHandler->ADEnabled : false;
    int sp = dashHandler ? (int)dashHandler->speedProfile : 0;
    bool spAuto = dashHandler ? (bool)dashHandler->speedProfileAuto : true;
    int soff = dashHandler ? (int)dashHandler->speedOffset : 0;
    int gtwAp = dashHandler ? (int)dashHandler->gatewayAutopilot : -1;
    bool ep = dashHandler ? (bool)dashHandler->enablePrint : true;
    bool apGateOpen = dashApInjectionAllowed();
    uint8_t effectiveHw = (hwMode == 3) ? DASH_DEFAULT_HW : hwMode;
    const char *hwName = "LEGACY";
    if (effectiveHw == 1)
        hwName = "HW3";
    else if (effectiveHw == 2)
        hwName = "HW4";

    // Capability matrix will be appended later after Phase 1 fields.
    // (we need effectiveHw which is computed here)

    String j = "{\"hw\":";
    j += hwMode;
    j += ",\"dashDefaultHw\":";
    j += DASH_DEFAULT_HW;
    j += ",\"effectiveHw\":";
    j += effectiveHw;
    j += ",\"hwName\":\"";
    j += hwName;
    j += "\"";
    j += ",\"buildEnv\":\"" DASH_BUILD_ENV "\"";
    j += ",\"uiBuildId\":\"" DASH_UI_BUILD_ID "\"";
    j += ",\"uiBuildUtc\":\"" DASH_UI_BUILD_UTC "\"";
    j += ",\"";
    j += "capabilities";
    j += "\":"; // "capabilities":
    j += dashCapabilitiesJson();
    j += ",\"sp\":";
    j += sp;
    j += ",\"spAuto\":";
    j += spAuto ? "true" : "false";
    j += ",\"driveProfile\":";
    j += dashDriveProfile;
    j += ",\"driveProfileName\":\"";
    j += dashDriveProfileName(dashDriveProfile);
    j += "\"";
    j += ",\"soff\":";
    j += soff;
    j += ",\"gtwap\":";
    j += gtwAp;
    j += ",\"AD\":";
    j += APActive ? "true" : "false";
    j += ",\"apActive\":";
    j += APActive ? "true" : "false";
    j += ",\"adEnabled\":";
    j += ADEnabled ? "true" : "false";
    j += ",\"eprn\":";
    j += ep ? "true" : "false";
    j += ",\"force\":";
    j += forceActivate ? "true" : "false";
    j += ",\"apGate\":";
    j += (canActive && !apGateOpen) ? "true" : "false";
    j += ",\"apGateOpen\":";
    j += apGateOpen ? "true" : "false";
    j += ",\"apGateEnabled\":";
    j += apInjectionGate ? "true" : "false";
    j += ",\"apAutoRestore\":";
    j += apAutoRestore ? "true" : "false";
    j += ",\"apDelayMs\":";
    j += legacyFsdRequiredStableMs;
    j += ",\"apInjectionState\":\"";
    j += dashApInjectionStateName();
    j += "\"";
    j += ",\"injectionSource\":\"";
#if defined(DASH_PLUGIN_ENGINE)
    j += dashPluginOwnsFsdActivation() ? "Plugin" : (canActive ? "Built-in" : "Disabled");
#else
    j += canActive ? "Built-in" : "Disabled";
#endif
    j += "\"";
    j += ",\"ia\":";
    j += dashInjectionActive() ? "true" : "false";
    j += ",\"lastInjectMs\":";
    j += (uint32_t)lastInjectMs;
    j += ",\"hw3OffsetSlew\":";
    j += hw3OffsetSlew ? "true" : "false";
    j += ",\"hw3SlewRate\":";
    j += hw3SlewRate;
    j += ",\"hw3OffsetTarget\":";
    j += hw3OffsetTargetRaw;
    j += ",\"hw3OffsetLast\":";
    j += hw3OffsetLastRaw;
    j += ",\"hw3SlewCount\":";
    j += hw3OffsetSlewCount;
    // HW3 custom speed-limit boost
    j += ",\"hw3CustomSpeed\":";
    j += hw3CustomSpeed ? "true" : "false";
    j += ",\"hw3CustomTarget\":[";
    for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
    {
        if (i)
            j += ",";
        j += hw3CustomTarget[i];
    }
    j += "],\"hw3HighSpeedEnable\":";
    j += hw3HighSpeedEnable ? "true" : "false";
    j += ",\"hw3HighSpeedTarget\":[";
    for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
    {
        if (i)
            j += ",";
        j += hw3HighSpeedTarget[i];
    }
    j += "],\"hw3WireEncoding\":";
    j += hw3WireEncoding;
    j += ",\"fusedSpeedLimitRaw\":";
    j += fusedSpeedLimitRaw;
    j += ",\"fusedSpeedLimitKph\":";
    j += (fusedSpeedLimitRaw == 0 || fusedSpeedLimitRaw == 31)
             ? 0
             : (uint16_t)fusedSpeedLimitRaw * 5;
    j += ",\"speedLimit\":";
    j += (fusedSpeedLimitRaw == 0 || fusedSpeedLimitRaw == 31)
             ? 0
             : (uint16_t)fusedSpeedLimitRaw * 5;
    j += ",\"actOffset\":";
    j += String((float)actualOffset, 1);
    j += ",\"hw3StockOffset\":";
    j += hw3StockOffsetKph;
    // Legacy MPP custom speed-limit override
    j += ",\"legacyMppOverride\":";
    j += legacyMppOverride ? "true" : "false";
    j += ",\"legacyMppCustomEnable\":";
    j += legacyMppCustomEnable ? "true" : "false";
    j += ",\"legacyMppCustomTarget\":[";
    for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
    {
        if (i)
            j += ",";
        j += legacyMppCustomTarget[i];
    }
    j += "],\"legacyMppHighSpeedEnable\":";
    j += legacyMppHighSpeedEnable ? "true" : "false";
    j += ",\"legacyMppHighSpeedTarget\":[";
    for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
    {
        if (i)
            j += ",";
        j += legacyMppHighSpeedTarget[i];
    }
    j += "],\"legacyMppLastRaw\":";
    j += legacyMppLastRaw;
    j += ",\"legacyMppLastSentRaw\":";
    j += legacyMppLastSentRaw;
    // --- FSD runtime state fields (runtime switches) ---
    j += ",\"fsdTriggered\":";
    j += dashHandler ? ((bool)dashHandler->fsdTriggered ? "true" : "false") : "false";
    appendTwaiDiagJson(j, now);
    appendFsdDiagJson(j, now);
    j += ",\"hwDetected\":";
    j += dashHandler ? (int)dashHandler->hwDetected : 0;
    j += ",\"autoMode\":";
    j += dashHandler ? ((bool)dashHandler->autoModeEnabled ? "true" : "false") : "false";
    j += ",\"tlsscBypass\":";
    j += dashHandler ? ((bool)dashHandler->tlsscBypass ? "true" : "false") : "false";
    j += ",\"isaChimeSuppress\":";
    j += dashHandler ? ((bool)dashHandler->isaChimeSuppress ? "true" : "false") : "false";
    j += ",\"isaOverride\":";
    j += dashHandler ? ((bool)dashHandler->isaOverride ? "true" : "false") : "false";
    j += ",\"evd\":";
    j += dashHandler ? ((bool)dashHandler->emergencyVehicleDetection ? "true" : "false") : "false";
    j += ",\"hw4OffsetRaw\":";
    j += dashHandler ? (int)dashHandler->hw4OffsetRaw : 0;
    j += ",\"banShield\":";
    j += dashHandler ? ((bool)dashHandler->banShieldEnable ? "true" : "false") : "false";
    j += ",\"banShieldBlocks\":";
    j += dashHandler ? (uint32_t)dashHandler->banShieldBlocks : 0;
    j += ",\"legacyOffset\":";
    j += dashHandler ? (int)dashHandler->legacyOffset : 0;
    j += ",\"removeVisionSpeedLimit\":";
    j += dashHandler ? ((bool)dashHandler->removeVisionSpeedLimit ? "true" : "false") : "false";
    j += ",\"overrideSpeedLimit\":";
    j += dashHandler ? ((bool)dashHandler->overrideSpeedLimit ? "true" : "false") : "false";
    j += ",\"hw3AutoSpeed\":";
    j += hw3AutoSpeed ? "true" : "false";
    j += ",\"can\":";
    j += canOnline ? "true" : "false";
    j += ",\"ci\":";
    j += canActive ? "true" : "false";
    j += ",\"bootCan\":";
    j += bootCanActive ? "true" : "false";
    bool storedCan = canActive;
    bool storedForce = forceActivate;
    uint8_t storedHw = hwMode;
    Preferences statusPrefs;
    if (statusPrefs.begin(PREFS_NS, true))
    {
        storedCan = statusPrefs.getBool("can", canActive);
        storedForce = statusPrefs.getBool("force_act", forceActivate);
        storedHw = statusPrefs.getUChar("hw", hwMode);
        statusPrefs.end();
    }
    j += ",\"storedCan\":";
    j += storedCan ? "true" : "false";
    j += ",\"storedForce\":";
    j += storedForce ? "true" : "false";
    j += ",\"storedHw\":";
    j += storedHw;
    j += ",\"uptime\":";
    j += (millis() - startMs) / 1000;
    j += ",\"rx\":";
    j += rxCount;
    j += ",\"tx\":";
    j += txCount;
    j += ",\"txerr\":";
    j += txErrCount;
    j += ",\"fd\":";
    j += followDist;
    j += ",\"fps\":";
    {
        unsigned long fpsX10 = static_cast<unsigned long>(fps * 10.0f + 0.5f);
        j += String(fpsX10 / 10);
        j += ".";
        j += String(fpsX10 % 10);
    }
    j += ",\"eflg\":";
    j += mcpEflg;
    j += ",\"up\":";
    j += (millis() - startMs) / 1000;
    j += ",\"probe\":{\"active\":";
    j += dashWriteProbe.active ? "true" : "false";
    j += ",\"state\":";
    j += dashWriteProbe.state;
    j += ",\"id\":";
    j += dashWriteProbe.id;
    j += ",\"mux\":";
    j += dashWriteProbe.mux;
    j += ",\"txa\":";
    j += dashWriteProbe.active ? String(now - dashWriteProbe.txMs) : String(0);
    j += ",\"rxa\":";
    j += dashWriteProbe.hasRx ? String(now - dashWriteProbe.rxMs) : String(0);
    j += ",\"txdlc\":";
    j += dashWriteProbe.txDlc;
    j += ",\"rxdlc\":";
    j += dashWriteProbe.rxDlc;
    j += ",\"hasrx\":";
    j += dashWriteProbe.hasRx ? "true" : "false";
    j += ",\"tx\":[";
    for (uint8_t i = 0; i < dashWriteProbe.txDlc; i++)
    {
        if (i)
            j += ",";
        j += String(dashWriteProbe.txData[i]);
    }
    j += "],\"rx\":[";
    for (uint8_t i = 0; i < dashWriteProbe.rxDlc; i++)
    {
        if (i)
            j += ",";
        j += String(dashWriteProbe.rxData[i]);
    }
    j += "]},\"mux\":[";
    for (int i = 0; i < 3; i++)
    {
        if (i)
            j += ",";
        j += "{\"rx\":" + String(muxRx[i]) +
             ",\"tx\":" + String(muxTx[i]) +
             ",\"err\":" + String(muxErr[i]) + "}";
    }
    j += "]";
#ifdef DRIVER_T2CAN_DUAL
    j += ",\"can2\":{\"rx\":";
    j += t2canBus2RxCount();
    j += ",\"tx\":";
    j += t2canBus2TxCount();
    j += ",\"txerr\":";
    j += t2canBus2TxErrCount();
    j += ",\"eflg\":";
    j += t2canBus2Eflg();
    j += "}";
#endif
    appendServiceDiagJson(j, now);
    appendWheelDndDiagJson(j, now);
    // ── Phase 1 新增状态字段 ──────────────────────────────────────────
    j += ",\"vehicleOta\":";
    j += vehicleOtaActive ? "true" : "false";
    j += ",\"autoShutdown\":";
    j += autoShutdownEnabled ? "true" : "false";
    j += ",\"wifiAutoOff\":";
    j += wifiAutoOffEnabled ? "true" : "false";
    j += ",\"fogStrategy\":";
    j += (int)dashRearFogStrategy;
    j += ",\"strobeCont\":";
    j += dashFogCtrl.isActive() ? "true" : "false";
    j += ",\"dndVolume\":";
    j += dashDndVolume ? "true" : "false";
    j += ",\"dndSpeed\":";
    j += dashDndSpeed ? "true" : "false";
    appendCapabilitiesJson(j, hwMode, effectiveHw);
    j += "}";
    server.send(200, "application/json", j);
}

// GET /config — 返回运行时 FSD 开关供 UI 回填 (loadLegacyFsdConfig 读 conf.fsdRuntime.*)。
// 历史问题：曾只注册 POST /config，GET 无路由 → fetchJson('/config') 取 null →
// 重启后 legacyOffset 输入框/overrideSpeedLimit 开关恒显示默认（NVS 实际已持久）。
// 返回的 fsdRuntime 块与 handleConfig POST 接收的 {fsdRuntime:{...}} 契约对齐。
static void handleConfigGet()
{
    String j = "{\"fsdRuntime\":{";
    j += "\"legacyOffset\":" + String(nvsLegacyOffset);
    j += ",\"overrideSpeedLimit\":" + String(nvsOverrideSpeedLimit ? "true" : "false");
    j += "}}";
    server.send(200, "application/json", j);
}

static void handleConfig()
{
    // --- FSD runtime JSON body ({fsdRuntime:{legacyOffset,overrideSpeedLimit}}) ---
    // UI postConfigJson() posts here as application/json. Without this branch the
    // body was silently dropped and legacyOffset/overrideSpeedLimit never reached
    // NVS (reboot reset them). Must update nvs mirror THEN dashApplyRuntimeState,
    // else the next apply overwrites with the stale mirror.
    if (server.hasArg("plain"))
    {
        String body = server.arg("plain");
        JsonDocument doc;
        DeserializationError jsonErr = deserializeJson(doc, body);
        if (!jsonErr && doc["fsdRuntime"].is<JsonObject>())
        {
            JsonObject fsd = doc["fsdRuntime"].as<JsonObject>();
            bool changed = false;
            if (fsd["legacyOffset"].is<int>())
            {
                int offset = fsd["legacyOffset"].as<int>();
                if (offset < -30)
                    offset = -30;
                if (offset > 225)
                    offset = 225;
                nvsLegacyOffset = offset;
                changed = true;
            }
            if (fsd["overrideSpeedLimit"].is<bool>())
            {
                nvsOverrideSpeedLimit = fsd["overrideSpeedLimit"].as<bool>();
                changed = true;
            }
            if (changed)
            {
                dashApplyRuntimeState(); // nvs mirror -> dashHandler fields
                dashSavePrefs();         // dashHandler fields -> NVS commit
                dashLog("[CFG] fsdRuntime JSON: legacyOffset/overrideSpeedLimit applied");
            }
        }
    }
    bool hwChanged = false;
    if (server.hasArg("hw"))
    {
        uint8_t v = server.arg("hw").toInt();
        if (v <= 3 && v != hwMode)
        {
            hwMode = v;
            hwChanged = true;
            const char *hwName = v == 0 ? "LEGACY" : v == 1 ? "HW3"
                                                 : v == 2   ? "HW4"
                                                            : "AUTO";
            dashLog(String("[CFG] HW=") + hwName);
            // For hwMode=3 (auto), set autoModeEnabled on active handler;
            // the actual handler swap happens when CAN 920 detects HW version.
            if (dashHandler)
                dashHandler->autoModeEnabled = (v == 3);
        }
    }
    bool requestedFsdSwitch = canActive;
    bool hasFsdSwitchArg = false;
    if (server.hasArg("can"))
    {
        requestedFsdSwitch = server.arg("can") == "1";
        hasFsdSwitchArg = true;
    }
    if (server.hasArg("force"))
    {
        requestedFsdSwitch = server.arg("force") == "1";
        hasFsdSwitchArg = true;
    }
    if (server.hasArg("bootCan"))
    {
        bool v = server.arg("bootCan") == "1";
        if (v != bootCanActive)
        {
            bootCanActive = v;
            dashLog("[CFG] FSD boot auto-enable " + String(v ? "ON" : "OFF"));
        }
    }
    if (hasFsdSwitchArg && ((requestedFsdSwitch != canActive) || (requestedFsdSwitch != forceActivate)))
    {
        canActive = requestedFsdSwitch;
        forceActivate = requestedFsdSwitch;
        // Only update bootCanActive when explicitly provided via bootCan param
        dashLog("[CFG] FSD master switch " + String(requestedFsdSwitch ? "ON" : "OFF"));
    }
    bool profileAutoRequested = server.hasArg("spa") && server.arg("spa") == "1";
    if (server.hasArg("sp"))
    {
        uint8_t v = dashClampSpeedProfileForHw(hwMode, server.arg("sp").toInt());
        if (!profileAutoRequested && (v != dashManualSpeedProfile || dashSpeedProfileAuto))
            dashLog("[CFG] Speed profile manual " + String(v));
        dashManualSpeedProfile = v;
        if (!profileAutoRequested)
            dashSpeedProfileAuto = false;
    }
    if (server.hasArg("spa"))
    {
        bool v = server.arg("spa") == "1";
        if (v != dashSpeedProfileAuto)
            dashLog("[CFG] Speed profile " + String(v ? "AUTO" : "MANUAL"));
        dashSpeedProfileAuto = v;
    }
    if (server.hasArg("apRestore"))
    {
        bool v = server.arg("apRestore") == "1";
        if (v != apAutoRestore)
        {
            apAutoRestore = v;
            dashLog("[CFG] AP/EAP auto-restore " + String(v ? "ON" : "OFF"));
        }
    }
    if (server.hasArg("apg"))
    {
        bool v = server.arg("apg") == "1";
        if (v != apInjectionGate)
        {
            apInjectionGate = v;
            dashLog("[CFG] AP injection gate " + String(v ? "ON" : "OFF"));
        }
    }
    if (server.hasArg("ap_delay_ms"))
    {
        uint32_t v = dashClampApDelayMs(server.arg("ap_delay_ms").toInt());
        if (v != legacyFsdRequiredStableMs)
        {
            legacyFsdRequiredStableMs = v;
            dashLog("[CFG] AP settle delay " + String(v) + " ms");
        }
    }
    if (server.hasArg("ap_auto_restore"))
        apAutoRestore = dashArgTruthy(server.arg("ap_auto_restore"));
    // ap_rst kept as a stable alias for older clients / backup-restore payloads.
    if (server.hasArg("ap_rst"))
        apAutoRestore = dashArgTruthy(server.arg("ap_rst"));
    if (server.hasArg("hw3OffsetSlew"))
    {
        bool v = server.arg("hw3OffsetSlew") == "1";
        if (v != hw3OffsetSlew)
        {
            hw3OffsetSlew = v;
            dashLog("[CFG] HW3 offset slew " + String(v ? "ON" : "OFF"));
        }
    }
    if (server.hasArg("hw3SlewRate"))
    {
        uint8_t v = dashClampHw3SlewRate(server.arg("hw3SlewRate").toInt());
        if (v != hw3SlewRate)
        {
            hw3SlewRate = v;
            dashLog("[CFG] HW3 slew rate " + String(hw3SlewRate) + "%/s");
        }
    }
    // ─── HW3 custom speed-limit boost ────────────────────────────────────────
    if (server.hasArg("hw3CustomSpeed") || server.hasArg("hw3HighSpeedEnable") || server.hasArg("hw3AutoSpeed"))
    {
        bool legacyHw3CustomSpeed = hw3CustomSpeed;
        bool legacyHw3HighSpeedEnable = hw3HighSpeedEnable;
        bool legacyHw3AutoSpeed = hw3AutoSpeed;
        if (server.hasArg("hw3CustomSpeed"))
            legacyHw3CustomSpeed = server.arg("hw3CustomSpeed") == "1";
        if (server.hasArg("hw3HighSpeedEnable"))
            legacyHw3HighSpeedEnable = server.arg("hw3HighSpeedEnable") == "1";
        if (server.hasArg("hw3AutoSpeed"))
            legacyHw3AutoSpeed = server.arg("hw3AutoSpeed") == "1";
        dashSpeedStrategy = legacyHw3CustomSpeed ? 2 : ((legacyHw3HighSpeedEnable || legacyHw3AutoSpeed) ? 1 : 0);
        offsetMode = dashSpeedStrategy;
        dashSyncLegacyShims();
        dashLog(String("[CFG] HW3 speed strategy ") + dashSpeedStrategyName(dashSpeedStrategy));
    }
    if (server.hasArg("hw3WireEncoding"))
    {
        int v = server.arg("hw3WireEncoding").toInt();
        uint8_t enc = (v == kHw3WireEncKph5) ? kHw3WireEncKph5 : kHw3WireEncPct4;
        if (enc != hw3WireEncoding)
        {
            hw3WireEncoding = enc;
            dashLog(String("[CFG] HW3 wire enc ") +
                    (enc == kHw3WireEncPct4 ? "PCT4" : "KPH5"));
        }
    }
    {
        char arg[16];
        for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
        {
            snprintf(arg, sizeof(arg), "hw3CustomT%u", (unsigned)i);
            if (server.hasArg(arg))
                hw3CustomTarget[i] = dashClampHw3CustomTargetForBucket(i,
                                                                       server.arg(arg).toInt());
        }
        for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
        {
            snprintf(arg, sizeof(arg), "hw3HighTarget%u", (unsigned)i);
            if (server.hasArg(arg))
                hw3HighSpeedTarget[i] = dashClampHw3HighSpeedTargetForBucket(i,
                                                                             server.arg(arg).toInt());
        }
    }
    // ─── Legacy MPP custom speed-limit override ──────────────────────────────
    if (server.hasArg("legacyMppOverride"))
    {
        bool v = server.arg("legacyMppOverride") == "1";
        if (v != legacyMppOverride)
        {
            legacyMppOverride = v;
            dashLog("[CFG] Legacy MPP override " + String(v ? "ON" : "OFF"));
        }
    }
    if (server.hasArg("legacyMppCustomEnable"))
    {
        bool v = server.arg("legacyMppCustomEnable") == "1";
        if (v != legacyMppCustomEnable)
        {
            legacyMppCustomEnable = v;
            dashLog("[CFG] Legacy MPP custom " + String(v ? "ON" : "OFF"));
        }
    }
    if (server.hasArg("legacyMppHighSpeedEnable"))
    {
        bool v = server.arg("legacyMppHighSpeedEnable") == "1";
        if (v != legacyMppHighSpeedEnable)
        {
            legacyMppHighSpeedEnable = v;
            dashLog("[CFG] Legacy MPP high-speed " + String(v ? "ON" : "OFF"));
        }
    }
    {
        char arg[28];
        for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
        {
            snprintf(arg, sizeof(arg), "legacyMppCustomT%u", (unsigned)i);
            if (server.hasArg(arg))
                legacyMppCustomTarget[i] = dashClampLegacyMppCustomTargetForBucket(i,
                                                                                   server.arg(arg).toInt());
        }
        for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
        {
            snprintf(arg, sizeof(arg), "legacyMppHighTarget%u", (unsigned)i);
            if (server.hasArg(arg))
                legacyMppHighSpeedTarget[i] = dashClampLegacyMppHighSpeedTargetForBucket(i,
                                                                                         server.arg(arg).toInt());
        }
    }
    if (hwChanged)
    {
        dashSwapHandler(hwMode);
        dashApplyFilters();
    }
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleLoggingConfig()
{
    if (server.hasArg("eprn") && dashHandler)
    {
        bool ep = server.arg("eprn") == "1";
        dashHandler->enablePrint = ep;
        dashLog("[CFG] Logging " + String(ep ? "ON" : "OFF"));
    }
    dashApplyRuntimeState();
    dashSavePrefs();
    server.send(200, "application/json", "{\"ok\":true}");
}

static String dashModeHwJson()
{
    String j = "{\"ok\":true,\"mode\":\"";
    j += dashHwModeName(hwMode);
    j += "\",\"value\":";
    j += hwMode;
    j += ",\"options\":[\"Auto\",\"legacy\",\"HW3\",\"HW4\"]}";
    return j;
}

static void handleModeHw()
{
    if (server.hasArg("mode") || server.hasArg("value") || server.hasArg("hw"))
    {
        int next = -1;
        if (server.hasArg("mode"))
            next = dashHwModeFromName(server.arg("mode"));
        else if (server.hasArg("value"))
            next = server.arg("value").toInt();
        else if (server.hasArg("hw"))
            next = server.arg("hw").toInt();
        if (next < 0 || next > 3)
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"mode must be Auto, legacy, HW3, or HW4\"}");
            return;
        }
        if (static_cast<uint8_t>(next) != hwMode)
        {
            hwMode = static_cast<uint8_t>(next);
            dashSwapHandler(hwMode);
            dashApplyFilters();
            if (dashHandler)
                dashHandler->autoModeEnabled = (hwMode == 3);
            dashLog(String("[CFG] /mode_hw ") + dashHwModeName(hwMode));
        }
        dashApplyRuntimeState();
        dashSavePrefs();
    }
    server.send(200, "application/json", dashModeHwJson());
}

static String dashDriveProfileJson()
{
    uint8_t effective = dashSpeedProfileAuto ? 0 : dashManualSpeedProfile;
    String j = "{\"ok\":true,\"profile\":\"";
    j += dashDriveProfileName(dashDriveProfile);
    j += "\",\"value\":";
    j += dashDriveProfile;
    j += ",\"speed_profile_auto\":";
    j += dashSpeedProfileAuto ? "true" : "false";
    j += ",\"speed_profile\":";
    j += dashManualSpeedProfile;
    j += ",\"effective_speed_profile\":";
    j += effective;
    j += ",\"options\":[\"Auto\",\"Sloth\",\"Chill\",\"Normal\",\"Hurry\",\"MAX\"]}";
    return j;
}

static void handleDriveProfile()
{
    if (server.hasArg("profile") || server.hasArg("mode") || server.hasArg("value"))
    {
        int next = -1;
        if (server.hasArg("profile"))
            next = dashDriveProfileFromName(server.arg("profile"));
        else if (server.hasArg("mode"))
            next = dashDriveProfileFromName(server.arg("mode"));
        else if (server.hasArg("value"))
            next = server.arg("value").toInt();
        if (next < 0 || next > 5)
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"profile must be Auto, Sloth, Chill, Normal, Hurry, or MAX\"}");
            return;
        }
        dashDriveProfile = static_cast<uint8_t>(next);
        dashSpeedProfileAuto = dashDriveProfile == 0;
        if (!dashSpeedProfileAuto)
            dashManualSpeedProfile = dashClampSpeedProfileForHw(hwMode, dashSpeedProfileForDrive(dashDriveProfile));
        dashApplyRuntimeState();
        dashSavePrefs();
        dashLog(String("[CFG] /drive_profile ") + dashDriveProfileName(dashDriveProfile));
    }
    server.send(200, "application/json", dashDriveProfileJson());
}

static String dashSpeedStrategyJson()
{
    String j = "{\"ok\":true,\"strategy\":\"";
    j += dashSpeedStrategyName(dashSpeedStrategy);
    j += "\",\"value\":";
    j += dashSpeedStrategy;
    j += ",\"speed_profile_auto\":";
    j += dashSpeedProfileAuto ? "true" : "false";
    j += ",\"hw3_custom_speed\":";
    j += hw3CustomSpeed ? "true" : "false";
    j += ",\"legacy_custom_speed\":";
    j += legacyMppCustomEnable ? "true" : "false";
    j += ",\"options\":[\"fixed\",\"auto\",\"custom\"]}";
    return j;
}

static void handleSpeedStrategy()
{
    if (server.hasArg("strategy") || server.hasArg("value"))
    {
        int next = -1;
        if (server.hasArg("strategy"))
            next = dashSpeedStrategyFromName(server.arg("strategy"));
        else if (server.hasArg("value"))
            next = server.arg("value").toInt();
        if (next < 0 || next > 2)
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"strategy must be fixed, auto, or custom\"}");
            return;
        }
        dashSpeedStrategy = static_cast<uint8_t>(next);
        offsetMode = dashSpeedStrategy;
        dashSyncLegacyShims();
        if (dashSpeedStrategy == 1)
            dashSpeedProfileAuto = true;
        else
            dashSpeedProfileAuto = false;
        dashApplyRuntimeState();
        dashSavePrefs();
        dashLog(String("[CFG] /speed_strategy ") + dashSpeedStrategyName(dashSpeedStrategy));
    }
    server.send(200, "application/json", dashSpeedStrategyJson());
}

static String dashSpeedCustomJson()
{
    String j = "{\"ok\":true,\"customPct\":[";
    for (uint8_t i = 0; i < 4; i++)
    {
        if (i)
            j += ",";
        j += customPct[i];
    }
    j += "],\"manualPct\":";
    j += manualOffsetPct;
    j += ",\"cp1\":";
    j += customPct[0];
    j += ",\"cp2\":";
    j += customPct[1];
    j += ",\"cp3\":";
    j += customPct[2];
    j += ",\"cp4\":";
    j += customPct[3];
    j += "}";
    return j;
}

static void handleSpeedCustomGet()
{
    server.send(200, "application/json", dashSpeedCustomJson());
}

// POST /speed_custom — update custom zone percentages.
// HTTP params cp1..cp4 are 1-indexed → map to customPct[0..3] / NVS cp0..cp3.
// See dash_hw3_speed.h header for full mapping table.
static void handleSpeedCustom()
{
    bool changed = false;
    bool valid = true;
    bool hasManualPct = server.hasArg("manualPct");
    bool hasCp1 = server.hasArg("cp1");
    bool hasCp2 = server.hasArg("cp2");
    bool hasCp3 = server.hasArg("cp3");
    bool hasCp4 = server.hasArg("cp4");
    uint8_t next = 0;
    uint8_t nextManualPct = manualOffsetPct;
    uint8_t nextCustomPct[4] = {customPct[0], customPct[1], customPct[2], customPct[3]};

    if (hasManualPct)
    {
        valid = dashArgUIntInRange("manualPct", 0, 50, next) && valid;
        nextManualPct = next;
        changed = true;
    }
    if (hasCp1)
    {
        valid = dashArgUIntInRange("cp1", 0, 50, next) && valid;
        nextCustomPct[0] = next;
        changed = true;
    }
    if (hasCp2)
    {
        valid = dashArgUIntInRange("cp2", 0, 50, next) && valid;
        nextCustomPct[1] = next;
        changed = true;
    }
    if (hasCp3)
    {
        valid = dashArgUIntInRange("cp3", 0, 50, next) && valid;
        nextCustomPct[2] = next;
        changed = true;
    }
    if (hasCp4)
    {
        valid = dashArgUIntInRange("cp4", 0, 50, next) && valid;
        nextCustomPct[3] = next;
        changed = true;
    }
    if (!valid)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"manualPct/cp1..cp4 must be decimal integers from 0 to 50\"}");
        return;
    }
    if (changed)
    {
        manualOffsetPct = nextManualPct;
        customPct[0] = nextCustomPct[0];
        customPct[1] = nextCustomPct[1];
        customPct[2] = nextCustomPct[2];
        customPct[3] = nextCustomPct[3];
        dashSavePrefs();
        dashLog("[CFG] /speed_custom saved");
    }
    server.send(200, "application/json", dashSpeedCustomJson());
}

static String dashLightingConfigJson()
{
    String j = "{\"ok\":true,\"enabled\":";
    j += dashLightingEnabled ? "true" : "false";
    j += ",\"count\":";
    j += dashLightingCount;
    j += ",\"frequency\":\"";
    j += dashLightingFrequencyName(dashLightingFrequency);
    j += "\",\"frequency_value\":";
    j += dashLightingFrequency;
    j += ",\"rear_fog_strategy\":\"";
    j += dashRearFogStrategyName(dashRearFogStrategy);
    j += "\",\"rear_fog_value\":";
    j += dashRearFogStrategy;
    j += ",\"bus2_available\":";
#ifdef DRIVER_T2CAN_DUAL
    j += "true";
#else
    j += "false";
#endif
    j += "}";
    return j;
}

static void handleLightingConfig()
{
    if (server.hasArg("enabled") || server.hasArg("strobe") || server.hasArg("flash") ||
        server.hasArg("count") || server.hasArg("frequency") || server.hasArg("rear_fog_strategy"))
    {
        if (server.hasArg("enabled"))
            dashLightingEnabled = dashArgTruthy(server.arg("enabled"));
        if (server.hasArg("strobe") || server.hasArg("flash"))
            dashLightingEnabled = dashArgTruthy(server.hasArg("strobe") ? server.arg("strobe") : server.arg("flash"));
        if (server.hasArg("count"))
        {
            int count = server.arg("count").toInt();
            if (count == 3 || count == 5 || count == 7 || count == 10)
                dashLightingCount = static_cast<uint8_t>(count);
            else
            {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"count must be 3, 5, 7, or 10\"}");
                return;
            }
        }
        if (server.hasArg("frequency"))
        {
            int f = dashLightingFrequencyFromName(server.arg("frequency"));
            if (f < 0)
            {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"frequency must be slow, medium, or fast\"}");
                return;
            }
            dashLightingFrequency = static_cast<uint8_t>(f);
        }
        if (server.hasArg("rear_fog_strategy"))
        {
            int s = dashRearFogStrategyFromName(server.arg("rear_fog_strategy"));
            if (s < 0)
            {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"rear_fog_strategy must be off, strobe, or continuous\"}");
                return;
            }
            dashRearFogStrategy = static_cast<uint8_t>(s);
        }
        dashSavePrefs();
        dashLog("[CFG] /lighting_config saved");
    }
    server.send(200, "application/json", dashLightingConfigJson());
}

static String dashDefenseConfigJson()
{
    String j = "{\"ok\":true,\"enabled\":";
    j += dashDefenseEnabled ? "true" : "false";
    j += ",\"bionic_steering\":";
    j += dashBionicSteering ? "true" : "false";
    j += ",\"nag_torque_tamper\":";
    j += dashNagTorqueTamper ? "true" : "false";
    // Bionic disabled warning (3 consecutive failures)
    bool bionicDisabled = dashHandler ? dashHandler->bionicDisabled() : dashBionicDisabled;
    j += ",\"bionic_disabled\":";
    j += bionicDisabled ? "true" : "false";
    j += ",\"sound_warning_suppression\":";
    j += dashHandler ? ((bool)dashHandler->isaChimeSuppress ? "true" : "false") : (nvsIsaChimeSuppress ? "true" : "false");
    j += ",\"isa_override\":";
    j += dashHandler ? ((bool)dashHandler->isaOverride ? "true" : "false") : (nvsIsaOverride ? "true" : "false");
    j += ",\"dnd_volume\":";
    j += dashDndVolume ? "true" : "false";
    j += ",\"speed_no_disturb\":";
    j += dashSpeedNoDisturb ? "true" : "false";
    j += ",\"dnd_speed\":";
    j += dashDndSpeed ? "true" : "false";
    j += ",\"ap_eap_compatible\":";
    j += dashApEapCompatible ? "true" : "false";
    j += ",\"slew_rate_enabled\":";
    j += hw3OffsetSlew ? "true" : "false";
    j += ",\"ban_shield\":";
    j += dashHandler ? ((bool)dashHandler->banShieldEnable ? "true" : "false") : (nvsBanShieldEnable ? "true" : "false");
    j += "}";
    return j;
}

static void handleDefenseConfig()
{
    if (server.hasArg("enabled") || server.hasArg("bionic_steering") ||
        server.hasArg("sound_warning_suppression") || server.hasArg("speed_no_disturb") ||
        server.hasArg("ap_eap_compatible") || server.hasArg("dnd_volume") ||
        server.hasArg("dnd_speed") || server.hasArg("isa_override") ||
        server.hasArg("nag_torque_tamper"))
    {
        bool prevDefenseEnabled = dashDefenseEnabled;
        bool prevDndVolume = dashDndVolume;
        bool prevDndSpeed = dashDndSpeed;
        if (server.hasArg("enabled"))
            dashDefenseEnabled = dashArgTruthy(server.arg("enabled"));
        if (server.hasArg("bionic_steering"))
        {
            bool v = dashArgTruthy(server.arg("bionic_steering"));
            dashBionicSteering = v;
            // Reset bionic disabled state when user re-enables
            if (v)
                dashBionicDisabled = false;
            // Sync to NagHandler if available
            if (dashHandler)
            {
                dashHandler->bionicSteering = v;
                if (v)
                    dashHandler->resetBionic((uint32_t)millis());
            }
        }
        if (server.hasArg("nag_torque_tamper"))
        {
            // WARNING: torque-tamper is the documented primary-suspect vector of
            // the 2026-06-19 EPAS fault. Opt-in only; never the default.
            bool v = dashArgTruthy(server.arg("nag_torque_tamper"));
            dashNagTorqueTamper = v;
            nagTorqueTamperRuntime = v; // sync to NagHandler immediately
        }
        if (server.hasArg("sound_warning_suppression"))
        {
            bool v = dashArgTruthy(server.arg("sound_warning_suppression"));
            nvsIsaChimeSuppress = v;
            if (dashHandler)
                dashHandler->isaChimeSuppress = v;
        }
        if (server.hasArg("isa_override"))
        {
            bool v = dashArgTruthy(server.arg("isa_override"));
            nvsIsaOverride = v;
            if (dashHandler)
                dashHandler->isaOverride = v;
        }
        if (server.hasArg("dnd_volume"))
            dashDndVolume = dashArgTruthy(server.arg("dnd_volume"));
        if (server.hasArg("speed_no_disturb"))
            dashSpeedNoDisturb = dashArgTruthy(server.arg("speed_no_disturb"));
        if (server.hasArg("dnd_speed"))
            dashDndSpeed = dashArgTruthy(server.arg("dnd_speed"));
        if (!dashDefenseEnabled)
            dashWheelDndCtrl.reset();
        if (dashDefenseEnabled && dashDndVolume && (!prevDefenseEnabled || !prevDndVolume))
            dashWheelDndCtrl.startVolume();
        if (dashDefenseEnabled && dashDndSpeed && (!prevDefenseEnabled || !prevDndSpeed))
            dashWheelDndCtrl.startSpeed();
        if (server.hasArg("ap_eap_compatible"))
            dashApEapCompatible = dashArgTruthy(server.arg("ap_eap_compatible"));
        hw3OffsetSlew = dashDefenseEnabled;
        nvsBanShieldEnable = dashDefenseEnabled;
        if (dashHandler)
            dashHandler->banShieldEnable = dashDefenseEnabled;
        dashApplyRuntimeState();
        dashSavePrefs();
        dashLog("[CFG] /defense_config saved");
    }
    server.send(200, "application/json", dashDefenseConfigJson());
}

static void handleLegacyFsdConfig()
{
    if (server.hasArg("policy") || server.hasArg("mux1") || server.hasArg("profile") || server.hasArg("vision"))
    {
        if (server.hasArg("policy"))
        {
            String p = server.arg("policy");
            if (p == "legacy_tesla_parity" || p == "tesla_parity" || p == "2")
                dashLegacyFsdPolicy = LegacyFsdPolicy::TeslaParity;
            else if (p == "experimental" || p == "legacy_experimental" || p == "1")
                dashLegacyFsdPolicy = LegacyFsdPolicy::Experimental;
            else if (p == "stable" || p == "legacy_stable" || p == "0")
                dashLegacyFsdPolicy = LegacyFsdPolicy::Stable;
            else
                dashLegacyFsdPolicy = LegacyFsdPolicy::Stable;
        }
        if (server.hasArg("mux1"))
            dashLegacyFsdMux1Enable = server.arg("mux1") == "1";
        if (server.hasArg("profile"))
            dashLegacyFsdProfileWriteEnable = server.arg("profile") == "1";
        if (server.hasArg("vision"))
            dashLegacyFsdVisionLimitClearEnable = server.arg("vision") == "1";
        dashApplyRuntimeState();
        dashSavePrefs();
    }

    String j = "{\"policy\":\"";
    j += legacyFsdPolicyName(dashLegacyFsdPolicy);
    j += "\",\"mux1\":";
    j += dashLegacyFsdMux1Enable ? "true" : "false";
    j += ",\"profile\":";
    j += dashLegacyFsdProfileWriteEnable ? "true" : "false";
    j += ",\"vision\":";
    j += dashLegacyFsdVisionLimitClearEnable ? "true" : "false";
    j += "}";
    server.send(200, "application/json", j);
}

// ── Phase 1 新增端点 ──────────────────────────────────────────────

// POST/GET /power_mgmt — 功耗管理配置
static void handlePowerMgmt()
{
    dashPowerMgmtTouchWeb();
    if (server.hasArg("autoShutdown") || server.hasArg("wifiAutoOff"))
    {
        if (server.hasArg("autoShutdown"))
        {
            autoShutdownEnabled = dashArgTruthy(server.arg("autoShutdown"));
            if (autoShutdownEnabled)
                dashPowerMgmtConfigureWake();
        }
        if (server.hasArg("wifiAutoOff"))
            wifiAutoOffEnabled = dashArgTruthy(server.arg("wifiAutoOff"));
        prefs.begin(PREFS_NS, false);
        prefs.putBool(NVS_KEY_AUTO_SHUTDOWN, autoShutdownEnabled);
        prefs.putBool(NVS_KEY_WIFI_AUTO_OFF, wifiAutoOffEnabled);
        prefs.end();
        dashLog(String("[CFG] Power mgmt: shutdown=") + (autoShutdownEnabled ? "ON" : "OFF") +
                " wifi_off=" + (wifiAutoOffEnabled ? "ON" : "OFF"));
    }
    String json = "{\"autoShutdown\":";
    json += autoShutdownEnabled ? "true" : "false";
    json += ",\"wifiAutoOff\":";
    json += wifiAutoOffEnabled ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

// GET /vehicle_ota_status — 车辆OTA状态
static void handleVehicleOtaStatus()
{
    dashPowerMgmtTouchWeb();
    String json = "{\"vehicleOta\":";
    json += vehicleOtaActive ? "true" : "false";
    json += ",\"otaConfirmCount\":";
    json += String(otaConfirmCount);
    json += ",\"otaClearCount\":";
    json += String(otaClearCount);
    json += "}";
    server.send(200, "application/json", json);
}

// POST/GET /fog_light — 后雾灯策略 + 执行控制
static void handleFogLight()
{
    dashPowerMgmtTouchWeb();
    const char *reason = "idle";
#if defined(DRIVER_T2CAN_DUAL)
    bool driverSupported = true;
#else
    bool driverSupported = false;
#endif
    if (server.hasArg("fogStrategy"))
    {
        int strategy = server.arg("fogStrategy").toInt();
        if (strategy < 0 || strategy > 2)
            strategy = 0;
        dashRearFogStrategy = strategy;
        prefs.begin(PREFS_NS, false);
        prefs.putUChar("lt_fog", strategy);
        prefs.end();
        // Stop active fog when strategy changes to off; CAN task sends final OFF frame.
        if (strategy == 0)
            dashFogOffRequested = true;
        reason = "strategy_saved";
        dashLog(String("[CFG] Fog strategy: ") + dashRearFogStrategyName(strategy));
    }
    // Phase 4: trigger fog light execution. /lighting_config only saves the
    // preferred strategy; actual CAN-B execution must enter here.
    if (server.hasArg("trigger"))
    {
        String t = server.arg("trigger");
        if (!driverSupported)
        {
            reason = "driver_not_supported";
        }
        else if (t == "strobe")
        {
            dashFogCtrl.startStrobe(dashLightingCount, dashLightingFrequency);
            reason = "strobe_started";
            dashLog("[FOG] Strobe started");
        }
        else if (t == "f1")
        {
            dashFogCtrl.startF1Pilot();
            reason = "f1_started";
            dashLog("[FOG] F1 pilot started");
        }
        else if (t == "continuous")
        {
            dashFogCtrl.startContinuous();
            reason = "continuous_started";
            dashLog("[FOG] Continuous ON");
        }
        else if (t == "stop")
        {
            dashFogOffRequested = true;
            reason = "stop_requested";
            dashLog("[FOG] Stopped");
        }
        else
        {
            reason = "unknown_trigger";
        }
    }
    String j = "{\"ok\":true,\"fogStrategy\":";
    j += String(dashRearFogStrategy);
    j += ",\"active\":";
    j += dashFogCtrl.isActive() ? "true" : "false";
    j += ",\"canActive\":";
    j += canActive ? "true" : "false";
    j += ",\"driverSupported\":";
    j += driverSupported ? "true" : "false";
    j += ",\"reason\":\"";
    j += reason;
    j += "\"}";
    server.send(200, "application/json", j);
}

// POST /strobe_cont — 连续闪烁（Phase 4: now functional）
static void handleStrobeCont()
{
    dashPowerMgmtTouchWeb();
#if defined(DRIVER_T2CAN_DUAL)
    bool driverSupported = true;
#else
    bool driverSupported = false;
#endif
    bool enable = server.hasArg("enable") && server.arg("enable") == "1";
    const char *reason = "stop_requested";
    if (enable && driverSupported)
    {
        dashFogCtrl.startStrobe(0, dashLightingFrequency); // 0 = infinite
        reason = "continuous_started";
        dashLog("[STROBE] Continuous strobe started");
    }
    else if (enable)
    {
        reason = "driver_not_supported";
    }
    else
    {
        dashFogOffRequested = true;
        dashLog("[STROBE] Stopped");
    }
    String j = "{\"ok\":true,\"strobeCont\":";
    j += dashFogCtrl.isActive() ? "true" : "false";
    j += ",\"active\":";
    j += dashFogCtrl.isActive() ? "true" : "false";
    j += ",\"canActive\":";
    j += canActive ? "true" : "false";
    j += ",\"driverSupported\":";
    j += driverSupported ? "true" : "false";
    j += ",\"reason\":\"";
    j += reason;
    j += "\"}";
    server.send(200, "application/json", j);
}

static void handleGearAssistStatus()
{
    String j = "{\"ok\":true,\"available\":false";
    j += ",\"reason\":\"gear assist CAN control not implemented in this firmware stage\"";
    j += ",\"speed_kph\":";
    j += (dashHandler ? (int)dashHandler->speedOffset : 0);
    j += ",\"brake\":";
    j += apRestoreState.brakeSeen ? (apRestoreState.brakePedalRaw > 0 ? "true" : "false") : "false";
    j += ",\"brake_seen\":";
    j += apRestoreState.brakeSeen ? "true" : "false";
    j += ",\"gear\":\"";
    if (apRestoreState.gearSeen)
        j += String(apRestoreState.gearRaw);
    else
        j += "--";
    j += "\",\"gear_raw\":";
    j += apRestoreState.gearSeen ? String(apRestoreState.gearRaw) : String(-1);
    j += ",\"can_online\":";
    j += canOnline ? "true" : "false";
    j += "}";
    server.send(200, "application/json", j);
}

static String dashHotspotConfigJson(bool saved = false, bool reboot = false)
{
    String j = "{\"ok\":true,\"saved\":";
    j += saved ? "true" : "false";
    j += ",\"ssid\":\"";
    j += jsonEscape(apSSID);
    j += "\",\"has_password\":";
    j += strlen(apPass) > 0 ? "true" : "false";
    j += ",\"hidden\":";
    j += apHidden ? "true" : "false";
    j += ",\"ip\":\"";
    j += WiFi.softAPIP().toString();
    j += "\",\"clients\":";
    j += WiFi.softAPgetStationNum();
    j += ",\"reboot_required\":";
    j += reboot ? "true" : "false";
    j += "}";
    return j;
}

static void handleHotspotConfig()
{
    bool saved = false;
    bool reboot = false;
    if (server.hasArg("ssid") || server.hasArg("pass") || server.hasArg("hidden") || server.hasArg("save_reboot"))
    {
        String newSsid = server.hasArg("ssid") ? server.arg("ssid") : String(apSSID);
        String newPass = server.hasArg("pass") ? server.arg("pass") : String("");
        bool newHidden = server.hasArg("hidden") ? dashArgTruthy(server.arg("hidden")) : apHidden;
        reboot = server.hasArg("save_reboot") && dashArgTruthy(server.arg("save_reboot"));
        if (newSsid.length() == 0 || newSsid.length() > kDashMaxSsidLen)
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID must be 1-32 bytes\"}");
            return;
        }
        if (newPass.length() > 0 && !dashApPasswordLengthValid(newPass.length()))
        {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Password must be 8-64 characters\"}");
            return;
        }
        strlcpy(apSSID, newSsid.c_str(), sizeof(apSSID));
        if (newPass.length() > 0)
            strlcpy(apPass, newPass.c_str(), sizeof(apPass));
        apHidden = newHidden;
        prefs.begin(PREFS_NS, false);
        prefs.putString("ap_ssid", newSsid);
        if (newPass.length() > 0)
            prefs.putString("ap_pass", newPass);
        prefs.putBool("ap_hidden", apHidden);
        prefs.end();
        saved = true;
        dashLog("[WIFI] /hotspot_config saved");
    }
    server.send(200, "application/json", dashHotspotConfigJson(saved, reboot));
    if (saved && reboot)
    {
        delay(200);
        ESP.restart();
    }
}

static void handleRelayWifiTest()
{
    String ssid = server.hasArg("ssid") ? server.arg("ssid") : String(staSSID);
    bool valid = ssid.length() > 0 && ssid.length() <= kDashMaxSsidLen && !dashStaSsidLooksCorrupt(ssid);
    bool connectedSame = WiFi.status() == WL_CONNECTED && ssid == WiFi.SSID();
    String j = "{\"ok\":";
    j += valid ? "true" : "false";
    j += ",\"ssid\":\"";
    j += jsonEscape(ssid);
    j += "\",\"connected\":";
    j += connectedSame ? "true" : "false";
    j += ",\"wifi_status\":";
    j += WiFi.status();
    j += ",\"wifi_status_name\":\"";
    j += dashWifiStatusName(WiFi.status());
    j += "\",\"test_started\":false";
    j += ",\"non_disruptive\":true";
    if (!valid)
        j += ",\"error\":\"invalid SSID\"";
    else if (!connectedSame)
        j += ",\"message\":\"configuration accepted; use /wifi_config or /wifi_connect for an actual STA association\"";
    j += "}";
    server.send(valid ? 200 : 400, "application/json", j);
}

static void handleDnsRules()
{
#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)
    if (server.hasArg("enabled") || server.hasArg("blacklist") || server.hasArg("whitelist") ||
        server.hasArg("upstream_mode") || server.hasArg("upstream_custom"))
        handleGatewayDnsPost();
    else
        handleGatewayDnsGet();
#else
    server.send(501, "application/json", "{\"ok\":false,\"error\":\"DNS gateway not enabled\"}");
#endif
}

static void handleFrames()
{
    String j = "{\"frames\":[";
    int start = (sniffCount < SNIFFER_CAP) ? 0 : sniffHead;
    int count = min(sniffCount, SNIFFER_CAP);
    for (int i = 0; i < count; i++)
    {
        int idx = (start + i) % SNIFFER_CAP;
        SniffFrame &f = sniffBuf[idx];
        if (i)
            j += ",";
        j += "{\"ts\":" + String(f.ts) +
             ",\"id\":" + String(f.id) +
             ",\"dlc\":" + String(f.dlc) +
             ",\"data\":[";
        for (int b = 0; b < f.dlc; b++)
        {
            if (b)
                j += ",";
            j += String(f.data[b]);
        }
        j += "],\"name\":\"" + jsonEscape(decodeCanId(f.id)) + "\"}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleLog()
{
    // Pick up any new per-frame handler diagnostics first.
    dashDrainLogRing();
    unsigned long since = 0;
    if (server.hasArg("since"))
        since = strtoul(server.arg("since").c_str(), nullptr, 10);
    String j = "{\"seq\":";
    j += logSeq;
    j += ",\"lines\":[";
    int start = (logCount < LOG_CAP) ? 0 : logHead;
    int count = min(logCount, LOG_CAP);
    bool first = true;
    for (int i = 0; i < count; i++)
    {
        int idx = (start + i) % LOG_CAP;
        if (logBuf[idx].seq <= since)
            continue;
        if (!first)
            j += ",";
        first = false;
        j += "\"" + jsonEscape(logBuf[idx].msg) + "\"";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleResetStats()
{
    rxCount = 0;
    txCount = 0;
    txErrCount = 0;
    memset(muxRx, 0, sizeof(muxRx));
    memset(muxTx, 0, sizeof(muxTx));
    memset(muxErr, 0, sizeof(muxErr));
    dashResetWriteProbe();
    dashLog("[CFG] Stats reset");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleRecStart()
{
    if (!dashEnsureRecBuffer())
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"recorder buffer allocation failed\"}");
        return;
    }
    // Optional ID filter: /rec/start?ids=249,3E9,3F5 (hex, comma-separated).
    // Records only those IDs (on any bus) so the busy primary bus does not flood
    // the buffer while you capture lighting/stalk frames for checksum analysis.
    recFilterCount = 0;
    if (server.hasArg("ids"))
    {
        String s = server.arg("ids");
        const char *p = s.c_str();
        while (*p && recFilterCount < kRecFilterMax)
        {
            while (*p == ',' || *p == ' ')
                p++;
            if (!*p)
                break;
            char *end = nullptr;
            uint32_t v = static_cast<uint32_t>(strtoul(p, &end, 16));
            if (end == p)
                break; // no progress: avoid infinite loop on junk input
            recFilterIds[recFilterCount++] = v;
            p = end;
        }
    }
    recCount = 0;
    recSaved = false;
    recStartMs = millis();
    recActive = true;
    dashLog(recFilterCount > 0
                ? String("[REC] Recording started (filter ") + recFilterCount + " ids)"
                : String("[REC] Recording started"));
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleRecStop()
{
    bool ok = dashStopRecordingAndSave("manual");
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleRecStatus()
{
    if (recActive && (millis() - recStartMs >= kRecMaxDurationMs))
        dashStopRecordingAndSave("time limit");
    String j = "{\"active\":";
    j += recActive ? "true" : "false";
    j += ",\"count\":";
    j += recCount;
    j += ",\"cap\":";
    j += REC_CAP;
    j += ",\"saved\":";
    j += recSaved ? "true" : "false";
    j += ",\"psram\":";
    j += recBufInPsram ? "true" : "false";
    j += ",\"filter\":";
    j += recFilterCount;
    j += "}";
    server.send(200, "application/json", j);
}

#ifdef DRIVER_T2CAN_DUAL
// Defined in main.cpp — controls the bus B (X197 pin 9/10) service-mode injector
// and exposes the discovered-ID table sniffed on bus B.
void t2canSetServiceMode(bool on);
bool t2canGetServiceMode(void);
uint16_t t2canBus2IdCount(void);
uint32_t t2canBus2RxCount(void);
uint32_t t2canBus2TxCount(void);
uint32_t t2canBus2TxErrCount(void);
uint8_t t2canBus2Eflg(void);
const DashTxEvidence &t2canServiceTxEvidence(void);
uint8_t t2canServiceLastCommand(void);
uint8_t t2canServiceBurstRemaining(void);
bool t2canBus2IdAt(uint16_t i, uint16_t *id, uint8_t *dlc, uint8_t *data, uint32_t *count);
void t2canStalkTest(uint8_t status, uint16_t durationMs); // status 1=PULL flash, 2=PUSH high beam
void t2canSetBurstEnabled(bool on);
bool t2canGetBurstEnabled(void);
void t2canSetBurstParams(uint8_t count, uint16_t onMs, uint16_t offMs);
uint8_t t2canGetBurstCount(void);
uint16_t t2canGetBurstOnMs(void);
uint16_t t2canGetBurstOffMs(void);
bool t2canBurstIsRunning(void);
uint8_t t2canBurstPhasesLeft(void);
uint32_t t2canBurstLastPullMs(void);
uint32_t t2canBurstLastTriggerMs(void);
uint8_t t2canBurstPrevStatus(void);

static void handleServiceMode()
{
    if (server.hasArg("on"))
        t2canSetServiceMode(server.arg("on") == "1");
    String j = "{\"ok\":true,\"service_mode\":";
    j += t2canGetServiceMode() ? "true" : "false";
    appendServiceDiagJson(j, millis());
    j += "}";
    server.send(200, "application/json", j);
}

// Inject a short 0x249 stalk burst on bus B to test manual lighting:
//   /stalk_test?mode=flash     -> PULL (超车闪)  ~400ms
//   /stalk_test?mode=highbeam  -> PUSH (远光toggle) ~1000ms
static void handleStalkTest()
{
    String m = server.hasArg("mode") ? server.arg("mode") : "";
    uint8_t status = 0;
    uint16_t dur = 0;
    if (m == "highbeam")
    {
        status = 2;
        dur = 1000;
    }
    else if (m == "flash")
    {
        status = 1;
        dur = 400;
    }
    if (status && server.hasArg("dur"))
    {
        int requestedDur = server.arg("dur").toInt();
        if (requestedDur < 100)
            requestedDur = 100;
        if (requestedDur > 3000)
            requestedDur = 3000;
        dur = static_cast<uint16_t>(requestedDur);
    }
    if (status)
        t2canStalkTest(status, dur);
    server.send(200, "application/json",
                String("{\"ok\":") + (status ? "true" : "false") +
                    ",\"mode\":\"" + m + "\"" +
                    ",\"duration_ms\":" + String(dur) +
                    (status ? "" : ",\"error\":\"invalid mode\"") +
                    "}");
}

static void handleBurst()
{
    if (server.hasArg("enabled") || server.hasArg("on"))
    {
        String v = server.hasArg("enabled") ? server.arg("enabled") : server.arg("on");
        t2canSetBurstEnabled(v == "1" || v == "true" || v == "on");
    }
    if (server.hasArg("count") || server.hasArg("on_ms") || server.hasArg("off_ms"))
    {
        int count = t2canGetBurstCount();
        int onMs = t2canGetBurstOnMs();
        int offMs = t2canGetBurstOffMs();
        if (server.hasArg("count"))
            count = server.arg("count").toInt();
        if (server.hasArg("on_ms"))
            onMs = server.arg("on_ms").toInt();
        if (server.hasArg("off_ms"))
            offMs = server.arg("off_ms").toInt();
        t2canSetBurstParams(static_cast<uint8_t>(count), static_cast<uint16_t>(onMs), static_cast<uint16_t>(offMs));
    }
    String j = "{\"ok\":true,\"enabled\":";
    j += t2canGetBurstEnabled() ? "true" : "false";
    j += ",\"count\":";
    j += t2canGetBurstCount();
    j += ",\"on_ms\":";
    j += t2canGetBurstOnMs();
    j += ",\"off_ms\":";
    j += t2canGetBurstOffMs();
    j += ",\"running\":";
    j += t2canBurstIsRunning() ? "true" : "false";
    j += ",\"phases_left\":";
    j += t2canBurstPhasesLeft();
    j += ",\"last_pull_ms\":";
    j += t2canBurstLastPullMs();
    j += ",\"last_trigger_ms\":";
    j += t2canBurstLastTriggerMs();
    j += ",\"prev_status\":";
    j += t2canBurstPrevStatus();
    j += "}";
    server.send(200, "application/json", j);
}

static void handleBus2Ids()
{
    uint16_t n = t2canBus2IdCount();
    uint32_t total = 0;
    String j = "{\"count\":";
    j += n;
    j += ",\"service_mode\":";
    j += t2canGetServiceMode() ? "true" : "false";
    j += ",\"can2\":{\"rx\":";
    j += t2canBus2RxCount();
    j += ",\"tx\":";
    j += t2canBus2TxCount();
    j += ",\"txerr\":";
    j += t2canBus2TxErrCount();
    j += ",\"eflg\":";
    j += t2canBus2Eflg();
    j += "}";
    j += ",\"ids\":[";
    for (uint16_t i = 0; i < n; i++)
    {
        uint16_t id = 0;
        uint8_t dlc = 0;
        uint8_t data[8] = {0};
        uint32_t cnt = 0;
        if (!t2canBus2IdAt(i, &id, &dlc, data, &cnt))
            break;
        total += cnt;
        if (i)
            j += ",";
        char idbuf[8];
        snprintf(idbuf, sizeof(idbuf), "%03X", id);
        j += "{\"id\":\"";
        j += idbuf;
        j += "\",\"dlc\":";
        j += dlc;
        j += ",\"count\":";
        j += cnt;
        j += ",\"data\":\"";
        for (uint8_t b = 0; b < dlc && b < 8; b++)
        {
            char hb[4];
            snprintf(hb, sizeof(hb), "%02X", data[b]);
            if (b)
                j += " ";
            j += hb;
        }
        j += "\"}";
    }
    j += "],\"rx_total\":";
    j += total;
    j += "}";
    server.send(200, "application/json", j);
}
#endif

static void handleRecDownload()
{
    if (!SPIFFS.exists("/rec.csv"))
    {
        server.send(404, "text/plain", "No recording saved yet");
        return;
    }
    File f = SPIFFS.open("/rec.csv", "r");
    server.sendHeader("Content-Disposition", "attachment; filename=\"can_recording.csv\"");
    server.streamFile(f, "text/csv");
    f.close();
}

static void handleDisable()
{
    dashSetCanActive(false, "dashboard");
    server.send(200, "text/plain", "Injection stopped.");
}

static void handleReboot()
{
    server.send(200, "text/plain", "Rebooting...");
    delay(200);
    ESP.restart();
}

static void handleOtaCreds()
{
    // Credentials for dashboard OTA — only reachable by clients on the
    // same AP/STA network (protected by WiFi password at the link layer).
    String json = "{\"u\":\"" + String(DASH_OTA_USER) + "\",\"p\":\"" + String(DASH_OTA_PASS) + "\"}";
    server.send(200, "application/json", json);
}

static void handleOtaResult()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
    {
        server.requestAuthentication();
        return;
    }
    bool ok = Update.isFinished() && !Update.hasError();
    server.sendHeader("Connection", "close");
    server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
    if (ok)
    {
        dashLog("[OTA] Upload complete -- rebooting");
        delay(300);
        ESP.restart();
    }
    else
    {
        dashLog("[OTA] Upload FAILED: " + String(Update.errorString()));
        Update.abort();
    }
}

static void handleOtaUpload()
{
    if (!server.authenticate(DASH_OTA_USER, DASH_OTA_PASS))
        return;
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        dashLog("[OTA] Receiving: " + String(upload.filename.c_str()));
        esp_task_wdt_deinit();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            dashLog("[OTA] Begin failed: " + String(Update.errorString()));
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            dashLog("[OTA] Write error: " + String(Update.errorString()));
            Update.abort();
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (upload.totalSize > 0 && Update.end(true) && Update.isFinished())
            dashLog("[OTA] Done: " + String(upload.totalSize) + " bytes");
        else
        {
            dashLog("[OTA] End failed: " + String(Update.errorString()));
            Update.abort();
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        dashLog("[OTA] Upload aborted");
        Update.abort();
    }
}

#if defined(DASH_PLUGIN_ENGINE)
// --- Plugin manager persistence helpers -------------------------------------
// Plugin state is persisted as the engine's exportConfigJson() document
// (replayCount + per-plugin name/enabled/priority/source) so the full
// installed-plugin set survives reboots. save() writes it to SPIFFS after
// every state mutation; load() restores it once during dashboard setup. The
// file path is fixed so firmware and tooling agree on the location.
static constexpr const char *kDashPluginStatePath = "/plugins_state.json";

static void dashSavePluginState()
{
    if (!SPIFFS.begin(true))
        return;
    File f = SPIFFS.open(kDashPluginStatePath, "w");
    if (!f)
        return;
    std::string json = dashPluginEngine.exportConfigJson();
    f.print(json.c_str());
}

static void dashLoadPluginState()
{
    if (!SPIFFS.begin(true))
        return;
    if (!SPIFFS.exists(kDashPluginStatePath))
        return;
    File f = SPIFFS.open(kDashPluginStatePath, "r");
    if (!f)
        return;
    String body = f.readString();
    dashPluginEngine.importConfigJson(body.c_str());
}

// --- Plugin manager HTTP handlers -------------------------------------------
static void handlePluginsStatus()
{
    server.send(200, "application/json", String(dashPluginEngine.statusJson().c_str()));
}

static void handlePluginsInstallJson()
{
    if (!server.hasArg("plain") && !server.hasArg("json"))
    {
        server.send(400, "application/json", "{\"error\":\"json body required\"}");
        return;
    }
    String body = server.hasArg("plain") ? server.arg("plain") : server.arg("json");
    DashPluginResult r = dashPluginEngine.installJson(body.c_str(), false);
    dashSavePluginState();
    server.send(r.ok ? 200 : 400, "application/json",
                String("{\"ok\":") + (r.ok ? "true" : "false") + ",\"message\":\"" + r.message.c_str() + "\"}");
}

static void handlePluginsInstallUrl()
{
    if (!server.hasArg("url"))
    {
        server.send(400, "application/json", "{\"error\":\"url required\"}");
        return;
    }
    String url = server.arg("url");
    if (!url.startsWith("http://") && !url.startsWith("https://"))
    {
        server.send(400, "application/json", "{\"error\":\"http or https url required\"}");
        return;
    }
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.begin(client, url);
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        server.send(502, "application/json", "{\"error\":\"plugin fetch failed\"}");
        return;
    }
    String body = http.getString();
    http.end();
    DashPluginResult r = dashPluginEngine.installJson(body.c_str(), false);
    dashSavePluginState();
    server.send(r.ok ? 200 : 400, "application/json",
                String("{\"ok\":") + (r.ok ? "true" : "false") + ",\"message\":\"" + r.message.c_str() + "\"}");
}

// Upload chunk callback: only buffers the incoming bytes; the final response
// is sent by handlePluginsUpload() once the body is fully received (avoids a
// double-send across the WebServer fn + uploadFn callbacks).
static void handlePluginsUploadChunks()
{
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
        dashPluginUploadBuffer = "";
    else if (upload.status == UPLOAD_FILE_WRITE)
        dashPluginUploadBuffer.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_ABORTED)
        dashPluginUploadBuffer = "";
}

// Final response handler for the multipart upload route. WebServer invokes
// the upload callback per chunk and this handler exactly once after the body
// arrives, so install + persist + respond happen here.
static void handlePluginsUpload()
{
    DashPluginResult r = dashPluginEngine.installJson(dashPluginUploadBuffer.c_str(), false);
    dashSavePluginState();
    dashPluginUploadBuffer = "";
    server.send(r.ok ? 200 : 400, "application/json",
                String("{\"ok\":") + (r.ok ? "true" : "false") + ",\"message\":\"" + r.message.c_str() + "\"}");
}

static void handlePluginsToggle()
{
    DashPluginResult r = dashPluginEngine.setEnabled(server.arg("name").c_str(), dashArgTruthy(server.arg("enabled")));
    dashSavePluginState();
    server.send(r.ok ? 200 : 404, "application/json", String("{\"ok\":") + (r.ok ? "true" : "false") + "}");
}

static void handlePluginsPriority()
{
    DashPluginResult r = dashPluginEngine.setPriority(server.arg("name").c_str(),
                                                      static_cast<uint8_t>(server.arg("priority").toInt()));
    dashSavePluginState();
    server.send(r.ok ? 200 : 404, "application/json", String("{\"ok\":") + (r.ok ? "true" : "false") + "}");
}

static void handlePluginsRemove()
{
    DashPluginResult r = dashPluginEngine.remove(server.arg("name").c_str());
    dashSavePluginState();
    server.send(r.ok ? 200 : 404, "application/json", String("{\"ok\":") + (r.ok ? "true" : "false") + "}");
}

static void handlePluginsReplayCount()
{
    DashPluginResult r = dashPluginEngine.setReplayCount(server.arg("count").toInt());
    dashSavePluginState();
    server.send(r.ok ? 200 : 400, "application/json", String("{\"ok\":") + (r.ok ? "true" : "false") + "}");
}

static void handlePluginsRuleTest()
{
    server.send(200, "application/json", "{\"ok\":true,\"mode\":\"manual_preview_reserved\"}");
}
#endif

// CAN RUNTIME MANAGEMENT

static String dashFrameDataJson(const CanFrame &frame)
{
    String j = "[";
    for (uint8_t i = 0; i < 8; i++)
    {
        if (i)
            j += ",";
        j += String(frame.data[i]);
    }
    j += "]";
    return j;
}

static String dashFrameDataHex(const CanFrame &frame)
{
    String out;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (i)
            out += " ";
        if (frame.data[i] < 16)
            out += "0";
        out += String(frame.data[i], HEX);
    }
    out.toUpperCase();
    return out;
}

// ── WIFI STA ────────────────────────────────────────────────────

static bool dashStartAccessPoint(bool withSta)
{
    WiFi.persistent(false);
    WiFi.mode(withSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);

    IPAddress apIp(100, 100, 1, 1);
    IPAddress apMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, apMask);

    if (!dashApConfigValid(apSSID, apPass))
        dashUseDefaultApConfig();

    apRuntimeChannel = dashConfiguredApChannel();
    bool ok = WiFi.softAP(apSSID, apPass, apRuntimeChannel, apHidden ? 1 : 0, kDashApMaxConn);
    if (!ok)
    {
        dashUseDefaultApConfig();
        ok = WiFi.softAP(apSSID, apPass, apRuntimeChannel, 0, kDashApMaxConn);
    }
    if (!ok)
        dashLog("[WIFI] AP start failed");
    else
        dashGatewayOnApStarted(WiFi.apNetif());
    return ok;
}

static void dashBeginSTA()
{
    if (strlen(staSSID) == 0)
        return;

    // Ensure STA interface is enabled (AP+STA) before initiating a STA connect.
    // Without this, esp_wifi_set_config(WIFI_IF_STA, ...) inside WiFi.begin()
    // can fail silently when the device is in AP-only mode.
    if (WiFi.getMode() != WIFI_AP_STA)
        WiFi.mode(WIFI_AP_STA);

    if (staStaticIP && (uint32_t)staIP != 0)
    {
        WiFi.config(staIP, staGW, staMask, staDNS);
        dashLog("[WIFI] Static IP: " + staIP.toString());
    }
    else
    {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    // Disconnect any prior STA association before issuing a fresh begin().
    // Back-to-back WiFi.begin() without disconnect can leave esp_wifi in an
    // intermediate state and trigger an extra channel switch on the shared
    // AP+STA radio, which the AP beacon picks up as jitter (clients on
    // 100.100.1.1 momentarily see the AP go away). eraseAP=false keeps the
    // soft-AP up; wifioff=false keeps the radio on.
    WiFi.disconnect(false, false);
    WiFi.begin(staSSID, staPass);
    staConnectAttemptActive = true;
    staConnectStartedAt = millis();
    staRetryAt = 0;
    dashLog("[WIFI] Connecting to " + String(staSSID) + "...");
}

static void dashPrepareStaReconnect()
{
    if (staConnectAttemptActive || staConnected || WiFi.status() == WL_CONNECTED)
        WiFi.disconnect(false, false);
    dashGatewayOnStaDisconnected(WiFi.apNetif());
    staConnected = false;
    staConnectAttemptActive = false;
    staRetryAt = 0;
    staConsecutiveFailures = 0; // user-initiated reconnect resets diagnostics
    autoUpdateEligibleAt = 0;
}

static void dashApplyWifiSlot(uint8_t slot)
{
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    strlcpy(staSSID, n.ssid, sizeof(staSSID));
    strlcpy(staPass, n.pass, sizeof(staPass));
    staStaticIP = n.useStatic;
    if (n.useStatic)
    {
        staIP.fromString(n.ip);
        staGW.fromString(n.gw);
        staMask.fromString(n.mask);
        staDNS.fromString(n.dns);
    }
    else
    {
        staIP = IPAddress(0, 0, 0, 0);
    }
    wifiActiveSlot = static_cast<int8_t>(slot);
}

static void dashRotateAndConnect()
{
    if (wifiNetworkCount == 0)
        return;
    // Rotate through saved slots, skipping any slot whose SSID matches our own AP
    // (connecting to ourselves would bring down the AP and disconnect all clients).
    for (uint8_t tries = 0; tries < wifiNetworkCount; tries++)
    {
        uint8_t next = wifiNextRotateSlot % wifiNetworkCount;
        wifiNextRotateSlot = (next + 1) % wifiNetworkCount;
        dashApplyWifiSlot(next);
        if (strlen(apSSID) > 0 && strcmp(staSSID, apSSID) == 0)
        {
            dashLog("[WIFI] Skipping slot " + String(next) + " (matches own AP SSID)");
            continue;
        }
        dashLog("[WIFI] Trying slot " + String(next) + ": " + String(staSSID));
        // AP is already running in AP+STA mode since boot; don't restart it.
        // Only re-assert AP_STA mode if it was actually changed, to avoid
        // unnecessary esp_wifi_set_mode() calls that can briefly disturb the
        // shared AP+STA radio. dashBeginSTA() also sets AP_STA defensively.
        if (WiFi.getMode() != WIFI_AP_STA)
            WiFi.mode(WIFI_AP_STA);
        dashBeginSTA();
        return;
    }
    dashLog("[WIFI] No connectable STA slots (all match own AP SSID?)");
}

static void dashScheduleSTAConnect(unsigned long delayMs)
{
    if (strlen(staSSID) == 0)
        return;
    staConnectAttemptActive = false;
    staRetryAt = millis() + delayMs;
}

static void dashPrepareWifiScan()
{
    if (WiFi.getMode() != WIFI_AP_STA)
        WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
}

static uint8_t dashCurrentApChannel()
{
#ifdef ESP_PLATFORM
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK && cfg.ap.channel > 0)
        return cfg.ap.channel;
#endif
    return apRuntimeChannel;
}

static bool dashSyncApChannelToSta()
{
#ifdef ESP_PLATFORM
    wifi_ap_record_t staInfo = {};
    if (esp_wifi_sta_get_ap_info(&staInfo) != ESP_OK || staInfo.primary == 0)
        return false;

    uint8_t staChannel = staInfo.primary;
    uint8_t apChannel = dashCurrentApChannel();
    apLastChannelSyncTarget = staChannel;
    apLastChannelSyncMs = millis();

    if (apChannel == staChannel)
    {
        apRuntimeChannel = staChannel;
        apLastChannelSyncOk = true;
        dashLog("[WIFI] AP channel already matches STA CH" + String(staChannel));
        return true;
    }

    wifi_config_t cfg = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &cfg);
    if (err == ESP_OK)
    {
        cfg.ap.channel = staChannel;
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }

    if (err == ESP_OK)
    {
        apRuntimeChannel = staChannel;
        apLastChannelSyncOk = true;
        dashLog("[WIFI] AP channel auto matched: AP CH" + String(apChannel) +
                " -> STA CH" + String(staChannel));
        return true;
    }

    apLastChannelSyncOk = false;
    dashLog("[WIFI] AP channel auto match failed: AP CH" + String(apChannel) +
            " STA CH" + String(staChannel) + " err=" + String(esp_err_to_name(err)));
#endif
    return false;
}

static const char *dashWifiStatusName(int status)
{
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static void performAutoUpdate(); // forward decl, defined below

static void dashCheckWifi()
{
    static unsigned long lastCheck = 0;
    if (wifiNetworkCount == 0)
        return;
    unsigned long now = millis();
    if (!staConnected && !staConnectAttemptActive && staRetryAt > 0 && (long)(now - staRetryAt) >= 0)
    {
        staRetryAt = 0;
        dashRotateAndConnect();
        return; // let the fresh attempt age from its own start timestamp
    }

    unsigned long checkInterval = staConnectAttemptActive ? 250UL : 1000UL;
    if (now - lastCheck < checkInterval)
        return;
    lastCheck = now;

    int wifiStatus = WiFi.status();
    bool connected = wifiStatus == WL_CONNECTED;
    bool timedOut = !connected && staConnectAttemptActive &&
                    now - staConnectStartedAt >= kDashStaConnectTimeoutMs;
    if (timedOut)
    {
        uint8_t reason = WiFi.lastDisconnectReason();
        const char *reasonName = WiFi.lastDisconnectReasonName();
        staConnectAttemptActive = false;
        WiFi.disconnect(false, false);
        dashGatewayOnStaDisconnected(WiFi.apNetif());
        if (staConsecutiveFailures < 255)
            staConsecutiveFailures++;
        staRetryAt = now + kDashStaSavedPollMs;
        dashLog("[WIFI] STA connect timed out; status=" + String(dashWifiStatusName(wifiStatus)) +
                " reason=" + String(reasonName) + "(" + String(reason) + ")" +
                " retry saved networks in " + String(kDashStaSavedPollMs / 1000) +
                "s, AP+STA stays up (fail#" + String(staConsecutiveFailures) + ")");
        connected = false;
    }

    if (connected != staConnected)
    {
        staConnected = connected;
        if (connected)
        {
            staConnectAttemptActive = false;
            staRetryAt = 0;
            staConsecutiveFailures = 0; // reset diagnostics on successful connect
            dashLog("[WIFI] Connected to " + String(staSSID) + " IP: " + WiFi.localIP().toString());
            dashSyncApChannelToSta();
            dashGatewayOnStaConnected(WiFi.staNetif(), WiFi.apNetif());
            // Remember which slot just succeeded so the next reboot tries it
            // first. Avoids rotating through stale/dead networks on every boot.
            if (wifiActiveSlot >= 0 && wifiActiveSlot < (int8_t)wifiNetworkCount)
            {
                prefs.begin(PREFS_NS, false);
                if ((int8_t)prefs.getUChar("wn_pref", 0xFF) != wifiActiveSlot)
                    prefs.putUChar("wn_pref", static_cast<uint8_t>(wifiActiveSlot));
                prefs.end();
            }
            // Schedule auto-update check 15 s after STA comes up (grace period for other boot work)
            if (autoUpdateEnabled && !autoUpdateDone)
                autoUpdateEligibleAt = millis() + 15000;
        }
        else
        {
            if (staConsecutiveFailures < 255)
                staConsecutiveFailures++;
            dashLog("[WIFI] Disconnected from " + String(staSSID) +
                    "; retry saved networks in " + String(kDashStaSavedPollMs / 1000) + "s (fail#" +
                    String(staConsecutiveFailures) + ")");
            dashGatewayOnStaDisconnected(WiFi.apNetif());
            staConnectAttemptActive = false;
            staRetryAt = now + kDashStaSavedPollMs;
        }
    }

    // Fire one-shot auto-update check once eligible
    if (autoUpdateEnabled && !autoUpdateDone && staConnected && autoUpdateEligibleAt > 0 && millis() >= autoUpdateEligibleAt)
    {
        autoUpdateDone = true;
        performAutoUpdate();
    }
}

// Cached scan results — a full-channel scan in APSTA mode briefly drops the
// AP beacon, so we do NOT scan more often than kDashScanMinIntervalMs even if
// the WebUI keeps polling. Cached JSON is returned for repeat calls inside the
// window, and a 429 with retry-after is returned if the cache is empty.
static String dashCachedScanJson;
static unsigned long dashLastScanAt = 0;
static constexpr unsigned long kDashScanMinIntervalMs = 30000;

static void handleWifiScan()
{
    unsigned long now = millis();
    bool force = server.hasArg("force") && server.arg("force") == "1";
    if (!force && dashLastScanAt != 0 && (now - dashLastScanAt) < kDashScanMinIntervalMs)
    {
        if (dashCachedScanJson.length() > 0)
        {
            server.send(200, "application/json", dashCachedScanJson);
        }
        else
        {
            unsigned long retryMs = kDashScanMinIntervalMs - (now - dashLastScanAt);
            server.sendHeader("Retry-After", String((retryMs + 999) / 1000).c_str());
            server.send(429, "application/json",
                        String("{\"ok\":false,\"error\":\"scan-throttled\",\"retry_ms\":") +
                            String(retryMs) + "}");
        }
        return;
    }
    // Delete any lingering async scan result before triggering a new blocking scan.
    // In APSTA mode a full-channel scan briefly pauses AP beacon delivery.
    // This is user-triggered only; we do NOT scan automatically on reconnect.
    WiFi.scanDelete();
    dashPrepareWifiScan();
    int n = WiFi.scanNetworks(false, false, false, 300);
    String j = "{\"networks\":[";
    for (int i = 0; i < n && i < 20; i++)
    {
        if (i)
            j += ",";
        j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i).c_str()) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        j += ",\"enc\":" + String(auth != WIFI_AUTH_OPEN ? "true" : "false");
        j += ",\"auth\":" + String(static_cast<int>(auth));
        j += ",\"ch\":" + String(WiFi.channel(i));
        j += "}";
    }
    j += "]}";
    WiFi.scanDelete();
    dashCachedScanJson = j;
    dashLastScanAt = millis();
    server.send(200, "application/json", j);
}

static void dashPersistWifiSlot(uint8_t slot)
{
    if (slot >= wifiNetworkCount)
        return;
    const DashWifiNetwork &n = wifiNetworks[slot];
    prefs.putString(dashWifiKey(slot, "s").c_str(), String(n.ssid));
    prefs.putString(dashWifiKey(slot, "p").c_str(), String(n.pass));
    prefs.putBool(dashWifiKey(slot, "t").c_str(), n.useStatic);
    if (n.useStatic)
    {
        prefs.putString(dashWifiKey(slot, "i").c_str(), String(n.ip));
        prefs.putString(dashWifiKey(slot, "g").c_str(), String(n.gw));
        prefs.putString(dashWifiKey(slot, "m").c_str(), String(n.mask));
        prefs.putString(dashWifiKey(slot, "d").c_str(), String(n.dns));
    }
    else
    {
        prefs.remove(dashWifiKey(slot, "i").c_str());
        prefs.remove(dashWifiKey(slot, "g").c_str());
        prefs.remove(dashWifiKey(slot, "m").c_str());
        prefs.remove(dashWifiKey(slot, "d").c_str());
    }
}

static void dashRemoveWifiSlotKeys(uint8_t slot)
{
    prefs.remove(dashWifiKey(slot, "s").c_str());
    prefs.remove(dashWifiKey(slot, "p").c_str());
    prefs.remove(dashWifiKey(slot, "t").c_str());
    prefs.remove(dashWifiKey(slot, "i").c_str());
    prefs.remove(dashWifiKey(slot, "g").c_str());
    prefs.remove(dashWifiKey(slot, "m").c_str());
    prefs.remove(dashWifiKey(slot, "d").c_str());
}

// Save to slot N (0..count). idx == count means append (new). Reconnect on save.
static void handleWifiConfig()
{
    if (!server.hasArg("ssid"))
    {
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0 || ssid.length() > kDashMaxSsidLen || dashStaSsidLooksCorrupt(ssid))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID or password\"}");
        return;
    }

    int idx = -1;
    if (server.hasArg("idx"))
        idx = server.arg("idx").toInt();
    if (idx < 0 || idx > wifiNetworkCount)
        idx = wifiNetworkCount; // append

    if (idx == kDashMaxWifiNetworks)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Max networks reached\"}");
        return;
    }

    String effectivePass = pass;
    if (idx >= 0 && idx < wifiNetworkCount && pass.length() == 0 && strlen(wifiNetworks[idx].pass) > 0)
        effectivePass = wifiNetworks[idx].pass;
    if (!dashStaConfigLengthValid(ssid, effectivePass))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid SSID or password\"}");
        return;
    }

    DashWifiNetwork &n = wifiNetworks[idx];
    dashClearWifiNetwork(n);
    strlcpy(n.ssid, ssid.c_str(), sizeof(n.ssid));
    strlcpy(n.pass, effectivePass.c_str(), sizeof(n.pass));
    n.useStatic = server.hasArg("static") && server.arg("static") == "1";
    if (n.useStatic)
    {
        strlcpy(n.ip, server.arg("ip").c_str(), sizeof(n.ip));
        strlcpy(n.gw, server.arg("gw").c_str(), sizeof(n.gw));
        strlcpy(n.mask, server.arg("mask").c_str(), sizeof(n.mask));
        strlcpy(n.dns, server.arg("dns").c_str(), sizeof(n.dns));
    }

    if (idx == wifiNetworkCount)
        wifiNetworkCount++;

    prefs.begin(PREFS_NS, false);
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    dashPersistWifiSlot(idx);
    prefs.end();

    dashLog("[WIFI] Saved slot " + String(idx) + ": " + ssid);

    // Switch to newly saved slot and connect
    wifiNextRotateSlot = idx;
    dashApplyWifiSlot(idx);
    dashPrepareStaReconnect();

    server.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(idx) + "}");
    dashScheduleSTAConnect(1000);
}

static void handleWifiConnect()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
        return;
    }
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= wifiNetworkCount)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }

    dashApplyWifiSlot(static_cast<uint8_t>(idx));
    if (strlen(apSSID) > 0 && strcmp(staSSID, apSSID) == 0)
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"SSID matches AP hotspot\"}");
        return;
    }

    wifiNextRotateSlot = static_cast<uint8_t>(idx);
    dashPrepareStaReconnect();
    dashLog("[WIFI] Manual connect slot " + String(idx) + ": " + String(staSSID));

    server.send(200, "application/json",
                "{\"ok\":true,\"idx\":" + String(idx) +
                    ",\"ssid\":\"" + jsonEscape(staSSID) + "\"}");
    dashScheduleSTAConnect(100);
}

static void handleWifiDelete()
{
    if (!server.hasArg("idx"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
        return;
    }
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= wifiNetworkCount)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad idx\"}");
        return;
    }

    String removedSsid = wifiNetworks[idx].ssid;
    // Shift slots down
    for (uint8_t i = idx; i + 1 < wifiNetworkCount; i++)
        wifiNetworks[i] = wifiNetworks[i + 1];
    wifiNetworkCount--;
    dashClearWifiNetwork(wifiNetworks[wifiNetworkCount]);

    // Rewrite all slot keys
    prefs.begin(PREFS_NS, false);
    prefs.putUChar("wn_cnt", wifiNetworkCount);
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
        dashPersistWifiSlot(i);
    for (uint8_t i = wifiNetworkCount; i < kDashMaxWifiNetworks; i++)
        dashRemoveWifiSlotKeys(i);
    // Adjust persisted preferred slot to stay within bounds after deletion.
    // If the deleted slot WAS the preferred one, reset to 0 so the next boot
    // doesn't try an empty/stale slot first.
    {
        uint8_t pref = prefs.getUChar("wn_pref", 0);
        if ((int)idx == (int)pref || pref >= wifiNetworkCount)
            prefs.putUChar("wn_pref", 0);
        else if (pref > idx)
            prefs.putUChar("wn_pref", pref - 1);
    }
    prefs.end();

    dashLog("[WIFI] Deleted slot " + String(idx) + ": " + removedSsid);

    // Adjust active slot if needed
    if (wifiActiveSlot == idx)
    {
        wifiActiveSlot = -1;
        if (staConnectAttemptActive || staConnected)
        {
            WiFi.disconnect(false, false);
            dashGatewayOnStaDisconnected(WiFi.apNetif());
            staConnectAttemptActive = false;
            staConnected = false;
        }
        if (wifiNetworkCount > 0)
        {
            wifiNextRotateSlot = 0;
            dashRotateAndConnect();
        }
        else
        {
            staSSID[0] = 0;
            staPass[0] = 0;
            // Keep AP+STA mode active even when no networks are saved.
            if (WiFi.getMode() != WIFI_AP_STA)
                WiFi.mode(WIFI_AP_STA);
        }
    }
    else if (wifiActiveSlot > idx)
    {
        wifiActiveSlot--;
    }
    if (wifiNextRotateSlot >= wifiNetworkCount)
        wifiNextRotateSlot = 0;

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiNetworks()
{
    String j = "{\"max\":";
    j += kDashMaxWifiNetworks;
    j += ",\"count\":";
    j += wifiNetworkCount;
    j += ",\"active\":";
    j += wifiActiveSlot;
    j += ",\"networks\":[";
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
    {
        if (i)
            j += ",";
        const DashWifiNetwork &n = wifiNetworks[i];
        j += "{\"idx\":";
        j += i;
        j += ",\"ssid\":\"" + jsonEscape(n.ssid) + "\"";
        j += ",\"hasPass\":" + String(strlen(n.pass) > 0 ? "true" : "false");
        j += ",\"static\":" + String(n.useStatic ? "true" : "false");
        if (n.useStatic)
        {
            j += ",\"ip\":\"" + String(n.ip) + "\"";
            j += ",\"gw\":\"" + String(n.gw) + "\"";
            j += ",\"mask\":\"" + String(n.mask) + "\"";
            j += ",\"dns\":\"" + String(n.dns) + "\"";
        }
        j += "}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

static void handleWifiStatus()
{
    bool stored = wifiNetworkCount > 0;
    bool connectedNow = WiFi.status() == WL_CONNECTED;
    IPAddress staIp = WiFi.localIP();
    bool connected = connectedNow;
    String activeSsid = connectedNow ? WiFi.SSID() : String(staSSID);
    if (dashStaSsidLooksCorrupt(activeSsid))
        activeSsid = "";
    String j = "{\"connected\":";
    j += connected ? "true" : "false";
    j += ",\"ssid\":\"" + jsonEscape(activeSsid) + "\"";
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"count\":" + String(wifiNetworkCount);
    j += ",\"active\":" + String(wifiActiveSlot);
    int wifiStatus = WiFi.status();
    j += ",\"wifi_status\":" + String(wifiStatus);
    j += ",\"wifi_status_name\":\"";
    j += dashWifiStatusName(wifiStatus);
    j += "\"";
    j += ",\"disconnect_reason\":";
    j += String(static_cast<unsigned>(WiFi.lastDisconnectReason()));
    j += ",\"disconnect_reason_name\":\"";
    j += WiFi.lastDisconnectReasonName();
    j += "\"";
    if (staConnectAttemptActive)
        j += ",\"attempt_age_s\":" + String((millis() - staConnectStartedAt) / 1000);
    if (connected)
        j += ",\"ip\":\"" + staIp.toString() + "\"";
    j += ",\"static\":" + String(staStaticIP ? "true" : "false");
    if (staStaticIP)
    {
        j += ",\"cfg_ip\":\"" + staIP.toString() + "\"";
        j += ",\"cfg_gw\":\"" + staGW.toString() + "\"";
        j += ",\"cfg_mask\":\"" + staMask.toString() + "\"";
        j += ",\"cfg_dns\":\"" + staDNS.toString() + "\"";
    }
    if (!connected)
        j += ",\"connecting\":" + String(staConnectAttemptActive ? "true" : "false");
    j += ",\"fail_count\":" + String(staConsecutiveFailures);
    if (!connected && staRetryAt > 0)
    {
        unsigned long now = millis();
        long retryInMs = (long)(staRetryAt - now);
        j += ",\"retry_in_s\":" + String(retryInMs > 0 ? (retryInMs / 1000) : 0);
    }
    j += "}";
    server.send(200, "application/json", j);
}

// ── AP Config (hotspot name/password) ───────────────────────────

#ifdef ESP_PLATFORM
static const char *dashResetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "other_wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

static bool dashReadTemperature(float &celsius)
{
    static temperature_sensor_handle_t tempHandle = nullptr;
    static bool tempReady = false;
    static bool tempTried = false;
    if (!tempTried)
    {
        tempTried = true;
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &tempHandle) == ESP_OK &&
            temperature_sensor_enable(tempHandle) == ESP_OK)
        {
            tempReady = true;
        }
    }
    return tempReady && temperature_sensor_get_celsius(tempHandle, &celsius) == ESP_OK;
}
#endif

#if defined(ESP_PLATFORM) && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static void dashReadCpuLoad(uint8_t &core0Load, uint8_t &core1Load, bool &valid)
{
    static uint32_t prevIdle[2] = {0, 0};
    static int64_t prevWallUs = 0;
    static bool havePrev = false;

    core0Load = 0;
    core1Load = 0;
    valid = false;

    constexpr UBaseType_t kMaxTasksForCpuStats = 48;
    TaskStatus_t tasks[kMaxTasksForCpuStats];
    configRUN_TIME_COUNTER_TYPE totalRunTime = 0;
    UBaseType_t count = uxTaskGetSystemState(tasks, kMaxTasksForCpuStats, &totalRunTime);
    if (count == 0)
        return;

    uint32_t idle[2] = {0, 0};
    bool seenIdle[2] = {false, false};
    for (UBaseType_t i = 0; i < count; i++)
    {
        const char *name = tasks[i].pcTaskName ? tasks[i].pcTaskName : "";
        if (strncmp(name, "IDLE", 4) != 0)
            continue;
        BaseType_t core = -1;
        size_t len = strlen(name);
        if (len > 0 && name[len - 1] >= '0' && name[len - 1] <= '1')
            core = name[len - 1] - '0';
        if (core >= 0 && core <= 1)
        {
            idle[core] = static_cast<uint32_t>(tasks[i].ulRunTimeCounter);
            seenIdle[core] = true;
        }
    }
    if (!seenIdle[0] || !seenIdle[1])
        return;

    int64_t nowUs = esp_timer_get_time();
    if (!havePrev)
    {
        prevIdle[0] = idle[0];
        prevIdle[1] = idle[1];
        prevWallUs = nowUs;
        havePrev = true;
        return;
    }

    uint32_t wallDelta = static_cast<uint32_t>(nowUs - prevWallUs);
    if (wallDelta < 100000)
        return;

    uint32_t idleDelta0 = idle[0] - prevIdle[0];
    uint32_t idleDelta1 = idle[1] - prevIdle[1];
    auto loadFromIdle = [](uint32_t idleDelta, uint32_t elapsedUs) -> uint8_t
    {
        uint32_t idlePct = elapsedUs ? (idleDelta * 100UL + elapsedUs / 2) / elapsedUs : 0;
        if (idlePct > 100)
            idlePct = 100;
        return static_cast<uint8_t>(100 - idlePct);
    };

    core0Load = loadFromIdle(idleDelta0, wallDelta);
    core1Load = loadFromIdle(idleDelta1, wallDelta);
    valid = true;
    prevIdle[0] = idle[0];
    prevIdle[1] = idle[1];
    prevWallUs = nowUs;
}
#else
static void dashReadCpuLoad(uint8_t &core0Load, uint8_t &core1Load, bool &valid)
{
    core0Load = 0;
    core1Load = 0;
    valid = false;
}
#endif

#ifndef DASH_BOARD_NAME
#define DASH_BOARD_NAME "ESP32-S3R8"
#endif

static void handleSystemStatus()
{
#ifdef ESP_PLATFORM
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flashSize = 0;
    esp_flash_get_size(NULL, &flashSize);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macText[18];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_ap_record_t apInfo;
    int rssi = 0;
    bool hasRssi = esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK;
    if (hasRssi)
        rssi = apInfo.rssi;

    size_t spiffsTotal = 0, spiffsUsed = 0;
    bool spiffsOk = esp_spiffs_info(NULL, &spiffsTotal, &spiffsUsed) == ESP_OK;

    const esp_partition_t *running = esp_ota_get_running_partition();
    uint32_t appUsed = 0;
    if (running)
    {
        esp_partition_pos_t runningPos = {};
        runningPos.offset = running->address;
        runningPos.size = running->size;
        esp_image_metadata_t imageMeta = {};
        if (esp_image_get_metadata(&runningPos, &imageMeta) == ESP_OK)
            appUsed = imageMeta.image_len;
    }
    float tempC = 0.0f;
    bool hasTemp = dashReadTemperature(tempC);
    uint32_t cpuHz = static_cast<uint32_t>(esp_clk_cpu_freq());
    uint32_t cpuMhz = (cpuHz + 500000UL) / 1000000UL;
    uint32_t apbMhz = (static_cast<uint32_t>(esp_clk_apb_freq()) + 500000UL) / 1000000UL;
    uint32_t xtalMhz = (static_cast<uint32_t>(esp_clk_xtal_freq()) + 500000UL) / 1000000UL;
    bool pmDynamic = false;
    int pmMinMhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    int pmMaxMhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#if CONFIG_PM_ENABLE
    esp_pm_config_t pmConfig;
    if (esp_pm_get_configuration(&pmConfig) == ESP_OK)
    {
        pmDynamic = pmConfig.min_freq_mhz != pmConfig.max_freq_mhz;
        pmMinMhz = pmConfig.min_freq_mhz;
        pmMaxMhz = pmConfig.max_freq_mhz;
    }
#endif
    wifi_mode_t wifiMode = WiFi.getMode();
    wifi_ps_type_t wifiPs = WIFI_PS_NONE;
    esp_wifi_get_ps(&wifiPs);
    const char *wifiModeText = "off";
    switch (wifiMode)
    {
    case WIFI_MODE_STA:
        wifiModeText = "STA";
        break;
    case WIFI_MODE_AP:
        wifiModeText = "AP";
        break;
    case WIFI_MODE_APSTA:
        wifiModeText = "AP+STA";
        break;
    default:
        wifiModeText = "off";
        break;
    }
    uint8_t cpu0Load = 0, cpu1Load = 0;
    bool hasCpuLoad = false;
    dashReadCpuLoad(cpu0Load, cpu1Load, hasCpuLoad);

    String j = "{\"chip\":\"ESP32-S3\"";
    j += ",\"module\":\"" DASH_BOARD_NAME "\"";
    j += ",\"board\":\"" DASH_BOARD_NAME "\"";
    j += ",\"target\":\"" CONFIG_IDF_TARGET "\"";
    j += ",\"cores\":" + String(chip.cores);
    j += ",\"revision\":" + String(chip.revision);
    j += ",\"cpu_mhz\":" + String(cpuMhz);
    j += ",\"cpu_default_mhz\":" + String(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    j += ",\"cpu_max_mhz\":240";
    j += ",\"cpu_core0_mhz\":" + String(cpuMhz);
    j += ",\"cpu_core1_mhz\":" + String(chip.cores > 1 ? cpuMhz : 0);
    j += ",\"cpu_policy\":\"" + String(pmDynamic ? "dynamic" : "fixed") + "\"";
    j += ",\"cpu_pm_min_mhz\":" + String(pmMinMhz);
    j += ",\"cpu_pm_max_mhz\":" + String(pmMaxMhz);
    j += ",\"cpu_overclock\":" + String(cpuMhz > 240 ? "true" : "false");
    j += ",\"cpu_load_valid\":" + String(hasCpuLoad ? "true" : "false");
    j += ",\"cpu0_load\":" + String(cpu0Load);
    j += ",\"cpu1_load\":" + String(cpu1Load);
    j += ",\"apb_mhz\":" + String(apbMhz);
    j += ",\"xtal_mhz\":" + String(xtalMhz);
    j += ",\"sram_bytes\":524288";
    j += ",\"rtc_sram_bytes\":16384";
    j += ",\"rom_bytes\":393216";
    j += ",\"idf\":\"" IDF_VER "\"";
    j += ",\"firmware\":\"" FIRMWARE_VERSION "\"";
    j += ",\"buildEnv\":\"" DASH_BUILD_ENV "\"";
    j += ",\"dashDefaultHw\":";
    j += DASH_DEFAULT_HW;
    j += ",\"uiBuildId\":\"" DASH_UI_BUILD_ID "\"";
    j += ",\"uiBuildUtc\":\"" DASH_UI_BUILD_UTC "\"";
    j += ",\"mac\":\"" + String(macText) + "\"";
    j += ",\"reset\":\"" + String(dashResetReasonName(esp_reset_reason())) + "\"";
    j += ",\"uptime\":" + String((millis() - startMs) / 1000);
    j += ",\"tasks\":" + String(uxTaskGetNumberOfTasks());
    j += ",\"core\":" + String(xPortGetCoreID());
    j += ",\"heap_total\":" + String(heap_caps_get_total_size(MALLOC_CAP_8BIT));
    j += ",\"heap_free\":" + String(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    j += ",\"heap_min\":" + String(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    j += ",\"heap_largest\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    j += ",\"internal_free\":" + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    j += ",\"psram_total\":" + String(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    j += ",\"psram_free\":" + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    j += ",\"flash_size\":" + String(flashSize);
    j += ",\"flash_speed\":" + String(80000000UL);
    j += ",\"app_addr\":" + String(running ? running->address : 0);
    j += ",\"app_size\":" + String(running ? running->size : 0);
    j += ",\"app_used\":" + String(appUsed);
    j += ",\"app_label\":\"" + String(running ? running->label : "") + "\"";
    j += ",\"spiffs_ok\":" + String(spiffsOk ? "true" : "false");
    j += ",\"spiffs_total\":" + String(spiffsTotal);
    j += ",\"spiffs_used\":" + String(spiffsUsed);
    j += ",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    j += ",\"wifi_mode\":\"" + String(wifiModeText) + "\"";
    j += ",\"wifi_sleep\":" + String(wifiPs == WIFI_PS_NONE ? "false" : "true");
    j += ",\"wifi_standard\":\"2.4GHz 802.11 b/g/n\"";
    j += ",\"wifi_max_mbps\":150";
    j += ",\"ble_supported\":" + String((chip.features & CHIP_FEATURE_BLE) ? "true" : "false");
#ifdef CONFIG_BT_ENABLED
    j += ",\"ble_enabled\":true";
    j += ",\"ble_status\":\"compiled\"";
#else
    j += ",\"ble_enabled\":false";
    j += ",\"ble_status\":\"firmware disabled\"";
#endif
    j += ",\"wifi_rssi\":";
    j += hasRssi ? String(rssi) : String("null");
    j += ",\"ap_clients\":" + String(WiFi.softAPgetStationNum());
    j += ",\"temp_c\":";
    if (hasTemp)
    {
        int tempX10 = (int)(tempC * 10.0f + (tempC >= 0 ? 0.5f : -0.5f));
        j += String(tempX10 / 10);
        j += ".";
        j += String(abs(tempX10 % 10));
    }
    else
    {
        j += "null";
    }
    j += "}";
    server.send(200, "application/json", j);
#else
    server.send(200, "application/json", "{\"chip\":\"native\",\"cores\":1}");
#endif
}

#ifdef ESP_PLATFORM
static const char *dashTaskStateName(eTaskState state)
{
    switch (state)
    {
    case eRunning:
        return "RUN";
    case eReady:
        return "READY";
    case eBlocked:
        return "BLOCK";
    case eSuspended:
        return "SUSP";
    case eDeleted:
        return "DEL";
    default:
        return "UNK";
    }
}

static String dashTaskCoreName(BaseType_t core)
{
    if (core == tskNO_AFFINITY)
        return "any";
    return String((int)core);
}

static BaseType_t dashTaskCore(TaskHandle_t handle)
{
    if (!handle)
        return tskNO_AFFINITY;
    return xTaskGetCoreID(handle);
}

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static constexpr UBaseType_t kMaxTasksForStats = 64;

struct DashTaskSample
{
    const TaskStatus_t *task;
    uint32_t delta;
};

static String dashBuildTaskStatsText(const TaskStatus_t *before,
                                     UBaseType_t beforeCount,
                                     const TaskStatus_t *after,
                                     UBaseType_t afterCount,
                                     uint32_t elapsedMs)
{
    DashTaskSample samples[kMaxTasksForStats];
    UBaseType_t sampleCount = 0;
    uint64_t totalDelta = 0;
    uint32_t idleDelta[2] = {0, 0};

    for (UBaseType_t i = 0; i < afterCount && i < kMaxTasksForStats; i++)
    {
        uint32_t prevCounter = static_cast<uint32_t>(after[i].ulRunTimeCounter);
        bool found = false;
        for (UBaseType_t j = 0; j < beforeCount && j < kMaxTasksForStats; j++)
        {
            if (before[j].xHandle == after[i].xHandle)
            {
                prevCounter = static_cast<uint32_t>(before[j].ulRunTimeCounter);
                found = true;
                break;
            }
        }
        uint32_t nowCounter = static_cast<uint32_t>(after[i].ulRunTimeCounter);
        uint32_t delta = found ? (nowCounter - prevCounter) : 0;
        samples[sampleCount++] = {&after[i], delta};
        totalDelta += delta;

        const char *name = after[i].pcTaskName ? after[i].pcTaskName : "";
        BaseType_t core = dashTaskCore(after[i].xHandle);
        if (strncmp(name, "IDLE", 4) == 0 && core >= 0 && core <= 1)
            idleDelta[core] = delta;
    }

    for (UBaseType_t i = 0; i < sampleCount; i++)
    {
        for (UBaseType_t j = i + 1; j < sampleCount; j++)
        {
            if (samples[j].delta > samples[i].delta)
            {
                DashTaskSample tmp = samples[i];
                samples[i] = samples[j];
                samples[j] = tmp;
            }
        }
    }

    auto pct = [totalDelta](uint32_t delta) -> String
    {
        if (totalDelta == 0)
            return "0.0";
        uint32_t tenths = static_cast<uint32_t>((static_cast<uint64_t>(delta) * 1000ULL + totalDelta / 2) / totalDelta);
        return String(tenths / 10) + "." + String(tenths % 10);
    };
    auto loadFromIdle = [totalDelta](uint32_t idle) -> String
    {
        if (totalDelta == 0)
            return "n/a";
        uint32_t perCoreTotal = static_cast<uint32_t>(totalDelta / 2);
        if (perCoreTotal == 0)
            return "n/a";
        uint32_t idlePct = static_cast<uint32_t>((static_cast<uint64_t>(idle) * 100ULL + perCoreTotal / 2) / perCoreTotal);
        if (idlePct > 100)
            idlePct = 100;
        return String(100 - idlePct);
    };

    String out;
    out.reserve(4096);
    out += "ev-open-can-tools task stats\n";
    out += "sample_ms: " + String(elapsedMs) + "\n";
    out += "tasks: " + String(afterCount) + "\n";
    out += "cpu0_load_from_idle: " + loadFromIdle(idleDelta[0]) + "%\n";
    out += "cpu1_load_from_idle: " + loadFromIdle(idleDelta[1]) + "%\n";
    out += "heap_free: " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT)) + " bytes\n";
    out += "heap_largest: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) + " bytes\n\n";
    out += "CPU%   core prio stack state  task\n";
    out += "-----  ---- ---- ----- ------ ----------------\n";

    for (UBaseType_t i = 0; i < sampleCount; i++)
    {
        const TaskStatus_t &t = *samples[i].task;
        String p = pct(samples[i].delta);
        while (p.length() < 5)
            p = " " + p;
        String core = dashTaskCoreName(dashTaskCore(t.xHandle));
        while (core.length() < 4)
            core = " " + core;
        String prio = String((unsigned)t.uxCurrentPriority);
        while (prio.length() < 4)
            prio = " " + prio;
        String stack = String((unsigned)t.usStackHighWaterMark);
        while (stack.length() < 5)
            stack = " " + stack;

        out += p + "  " + core + " " + prio + " " + stack + " ";
        out += dashTaskStateName(t.eCurrentState);
        out += "  ";
        out += t.pcTaskName ? t.pcTaskName : "?";
        out += "\n";
    }

    return out;
}

static String dashBuildTaskStatsTextBlocking()
{
    TaskStatus_t before[kMaxTasksForStats];
    TaskStatus_t after[kMaxTasksForStats];
    configRUN_TIME_COUNTER_TYPE totalBefore = 0;
    configRUN_TIME_COUNTER_TYPE totalAfter = 0;

    UBaseType_t beforeCount = uxTaskGetSystemState(before, kMaxTasksForStats, &totalBefore);
    int64_t startUs = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(1000));
    int64_t endUs = esp_timer_get_time();
    UBaseType_t afterCount = uxTaskGetSystemState(after, kMaxTasksForStats, &totalAfter);
    return dashBuildTaskStatsText(before, beforeCount, after, afterCount,
                                  static_cast<uint32_t>((endUs - startUs) / 1000));
}
#endif

static void handleTaskStats()
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    server.send(200, "text/plain; charset=utf-8", dashBuildTaskStatsTextBlocking());
#else
    server.send(200, "text/plain; charset=utf-8", "FreeRTOS runtime stats are not enabled.\n");
#endif
}
#else
static void handleTaskStats()
{
    server.send(200, "text/plain; charset=utf-8", "Task stats are only available on ESP-IDF builds.\n");
}
#endif

#ifdef ESP_PLATFORM
static void dashSerialPrintHelp()
{
    Serial.println();
    Serial.println("ev-open-can-tools serial diagnostics");
    Serial.println("Commands:");
    Serial.println("  help           show this help");
    Serial.println("  system_status  print CPU/heap/WiFi summary");
    Serial.println("  can_status     print CAN/injection summary");
    Serial.println("  task_stats     sample FreeRTOS tasks for 1s asynchronously");
    Serial.println();
}

static void dashSerialPrintSystemStatus()
{
    uint8_t cpu0Load = 0, cpu1Load = 0;
    bool hasCpuLoad = false;
    dashReadCpuLoad(cpu0Load, cpu1Load, hasCpuLoad);

    float tempC = 0.0f;
    bool hasTemp = dashReadTemperature(tempC);
    uint32_t cpuMhz = (static_cast<uint32_t>(esp_clk_cpu_freq()) + 500000UL) / 1000000UL;
    wifi_mode_t wifiMode = WiFi.getMode();

    const char *wifiModeText = "off";
    switch (wifiMode)
    {
    case WIFI_MODE_STA:
        wifiModeText = "STA";
        break;
    case WIFI_MODE_AP:
        wifiModeText = "AP";
        break;
    case WIFI_MODE_APSTA:
        wifiModeText = "AP+STA";
        break;
    default:
        break;
    }

    wifi_ap_record_t apInfo;
    bool hasRssi = esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK;

    Serial.println();
    Serial.println("[system_status]");
    Serial.printf("uptime=%lus firmware=%s idf=%s\n", (millis() - startMs) / 1000, FIRMWARE_VERSION, IDF_VER);
    Serial.printf("cpu=%luMHz load=", (unsigned long)cpuMhz);
    if (hasCpuLoad)
        Serial.printf("CPU0 %u%% CPU1 %u%%\n", cpu0Load, cpu1Load);
    else
        Serial.println("sampling");
    Serial.printf("heap_free=%u heap_largest=%u heap_min=%u tasks=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                  (unsigned)uxTaskGetNumberOfTasks());
    Serial.printf("wifi=%s connected=%s ap_clients=%u",
                  wifiModeText,
                  WiFi.status() == WL_CONNECTED ? "yes" : "no",
                  (unsigned)WiFi.softAPgetStationNum());
    if (hasRssi)
        Serial.printf(" rssi=%d", apInfo.rssi);
    Serial.println();
    if (hasTemp)
        Serial.printf("temp=%.1fC\n", tempC);
    Serial.println();
}

static void dashSerialPrintCanStatus()
{
    unsigned long fpsX10 = static_cast<unsigned long>(fps * 10.0f + 0.5f);
    bool apActive = dashHandler ? (bool)dashHandler->APActive : false;
    bool adEnabled = dashHandler ? (bool)dashHandler->ADEnabled : false;
    int sp = dashHandler ? (int)dashHandler->speedProfile : 0;
    bool spAuto = dashHandler ? (bool)dashHandler->speedProfileAuto : true;
    int gtwAp = dashHandler ? (int)dashHandler->gatewayAutopilot : -1;
    Serial.println();
    Serial.println("[can_status]");
    Serial.printf("can=%s fsd_switch=%s injection_active=%s hw=%u profile=%s/%d\n",
                  canOnline ? "online" : "offline",
                  canActive ? "ON" : "OFF",
                  dashInjectionActive() ? "ON" : "OFF",
                  (unsigned)hwMode,
                  spAuto ? "auto" : "manual",
                  sp);
    Serial.printf("rx=%lu tx=%lu txerr=%lu fps=%lu.%lu follow_dist=%u eflg=0x%02X\n",
                  rxCount, txCount, txErrCount, fpsX10 / 10, fpsX10 % 10,
                  (unsigned)followDist, (unsigned)mcpEflg);
    Serial.printf("APActive=%s ADEnabled=%s GTW_autopilot=%d\n",
                  apActive ? "yes" : "no",
                  adEnabled ? "yes" : "no",
                  gtwAp);
    Serial.println();
}

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static TaskStatus_t dashSerialTaskBefore[kMaxTasksForStats];
static UBaseType_t dashSerialTaskBeforeCount = 0;
static uint32_t dashSerialTaskStartMs = 0;
static bool dashSerialTaskSampling = false;

static void dashSerialStartTaskStats()
{
    configRUN_TIME_COUNTER_TYPE totalBefore = 0;
    dashSerialTaskBeforeCount = uxTaskGetSystemState(dashSerialTaskBefore, kMaxTasksForStats, &totalBefore);
    dashSerialTaskStartMs = millis();
    dashSerialTaskSampling = true;
    Serial.println("[task_stats] sampling 1000 ms...");
}

static void dashSerialTaskStatsTick()
{
    if (!dashSerialTaskSampling || millis() - dashSerialTaskStartMs < 1000)
        return;

    TaskStatus_t after[kMaxTasksForStats];
    configRUN_TIME_COUNTER_TYPE totalAfter = 0;
    UBaseType_t afterCount = uxTaskGetSystemState(after, kMaxTasksForStats, &totalAfter);
    uint32_t elapsedMs = millis() - dashSerialTaskStartMs;
    dashSerialTaskSampling = false;
    Serial.print(dashBuildTaskStatsText(dashSerialTaskBefore, dashSerialTaskBeforeCount, after, afterCount, elapsedMs));
}
#else
static void dashSerialStartTaskStats()
{
    Serial.println("FreeRTOS runtime stats are not enabled.");
}

static void dashSerialTaskStatsTick() {}
#endif

static void dashSerialRunCommand(char *cmd)
{
    char *start = cmd;
    while (*start == ' ' || *start == '\t')
        start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    for (char *p = start; *p; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = *p - 'A' + 'a';
    }

    if (strcmp(start, "help") == 0 || strcmp(start, "?") == 0)
        dashSerialPrintHelp();
    else if (strcmp(start, "system_status") == 0 || strcmp(start, "sys") == 0)
        dashSerialPrintSystemStatus();
    else if (strcmp(start, "can_status") == 0 || strcmp(start, "can") == 0)
        dashSerialPrintCanStatus();
    else if (strcmp(start, "task_stats") == 0 || strcmp(start, "tasks") == 0)
        dashSerialStartTaskStats();
    else if (*start)
        Serial.println("Unknown command. Type help.");
}

static void dashSerialDiagnosticsPoll()
{
    static char cmd[96];
    static uint8_t len = 0;
    static bool announced = false;

    if (!announced && millis() > 3000)
    {
        announced = true;
        Serial.println("[DIAG] Serial commands ready. Type help.");
    }

    dashSerialTaskStatsTick();

    int budget = 24;
    while (budget-- > 0 && Serial.available() > 0)
    {
        int ch = Serial.read();
        if (ch < 0)
            break;
        if (ch == '\r' || ch == '\n')
        {
            if (len > 0)
            {
                cmd[len] = '\0';
                dashSerialRunCommand(cmd);
                len = 0;
            }
            continue;
        }
        if (ch == 8 || ch == 127)
        {
            if (len > 0)
                len--;
            continue;
        }
        if (ch < 32 || ch > 126)
            continue;
        if (len < sizeof(cmd) - 1)
            cmd[len++] = static_cast<char>(ch);
    }
}
#else
static void dashSerialDiagnosticsPoll()
{
}
#endif

static bool dashCanGpioReserved(int pin)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (pin >= 26 && pin <= 32)
        return true; // embedded flash/PSRAM bus on ESP32-S3 modules
    if (pin == 45 || pin == 46)
        return true; // strapping/input-only pins
#elif defined(CONFIG_IDF_TARGET_ESP32)
#ifndef DASH_ALLOW_CAN_GPIO_6_11
#define DASH_ALLOW_CAN_GPIO_6_11 0
#endif
#if !DASH_ALLOW_CAN_GPIO_6_11
    if (pin >= 6 && pin <= 11)
        return true; // SPI flash pins on common ESP32 modules
#endif
#endif
    return false;
}

static bool dashCanGpioValid(int pin, bool tx)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int kMaxGpio = 48;
#else
    constexpr int kMaxGpio = 39;
#endif
    if (pin < 0 || pin > kMaxGpio || dashCanGpioReserved(pin))
        return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (tx && pin >= 34 && pin <= 39)
        return false; // input-only pins cannot drive TWAI TX
#else
    (void)tx;
#endif
    return true;
}

static void handleCanPins()
{
    Preferences canPrefs;
    bool customized = false;
    int tx = -1, rx = -1;
#if defined(DRIVER_TWAI)
    tx = (int)TWAI_TX_PIN;
    rx = (int)TWAI_RX_PIN;
#endif
    if (canPrefs.begin("can", false))
    {
        int storedTx = canPrefs.getChar("tx", -1);
        int storedRx = canPrefs.getChar("rx", -1);
        canPrefs.end();
        if (dashCanGpioValid(storedTx, true) && dashCanGpioValid(storedRx, false) && storedTx != storedRx)
        {
            tx = storedTx;
            rx = storedRx;
            customized = true;
        }
    }
    String j = "{\"tx\":" + String(tx);
    j += ",\"rx\":" + String(rx);
    j += ",\"customized\":" + String(customized ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

static void handleCanPinsSave()
{
    int tx = server.arg("tx").toInt();
    int rx = server.arg("rx").toInt();

    if (!dashCanGpioValid(tx, true) || !dashCanGpioValid(rx, false))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid or reserved GPIO for CAN\"}");
        return;
    }
    if (tx == rx)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"TX and RX must differ\"}");
        return;
    }

    Preferences canPrefs;
    if (!canPrefs.begin("can", false))
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }
    canPrefs.putChar("tx", (int8_t)tx);
    canPrefs.putChar("rx", (int8_t)rx);
    canPrefs.end();

    dashLog("[CAN] Pins saved: TX=" + String(tx) + " RX=" + String(rx) + " (reboot required)");
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
}

// ── Settings Backup / Restore ───────────────────────────────────

static void handleSettingsExport()
{
    Preferences p;
    String apSsid = "", apPass = "", wSsid = "", wPass = "";
    String wIp = "", wGw = "", wMask = "", wDns = "";
    bool wStatic = false, beta = false, autoUpdate = false, apHid = false;
    bool h3Slew = false, eprn = true;
    uint8_t h3SlewRate = kHw3SlewRateDefault;
    uint8_t storedHw = hwMode;
    bool storedCan = canActive;
    bool storedForce = forceActivate;
    bool storedBootCan = bootCanActive;
    bool storedApGate = apInjectionGate;
    bool storedApRestore = apAutoRestore;
    bool spAuto = dashSpeedProfileAuto;
    uint8_t spSel = dashManualSpeedProfile;
    uint8_t storedDriveProfile = dashDriveProfile;
    uint8_t storedSpeedStrategy = dashSpeedStrategy;
    uint8_t storedOffsetMode = offsetMode;
    uint8_t storedManualPct = manualOffsetPct;
    uint8_t storedCustomPct[4] = {customPct[0], customPct[1], customPct[2], customPct[3]};
    bool storedLightingEnabled = dashLightingEnabled;
    uint8_t storedLightingCount = dashLightingCount;
    uint8_t storedLightingFrequency = dashLightingFrequency;
    uint8_t storedRearFogStrategy = dashRearFogStrategy;
    bool storedDefenseEnabled = dashDefenseEnabled;
    bool storedBionicSteering = dashBionicSteering;
    bool storedNagTorqueTamper = dashNagTorqueTamper;
    bool storedSpeedNoDisturb = dashSpeedNoDisturb;
    bool storedDndVolume = dashDndVolume;
    bool storedDndSpeed = dashDndSpeed;
    bool storedApEapCompatible = dashApEapCompatible;
    bool storedAutoShutdown = autoShutdownEnabled;
    bool storedWifiAutoOff = wifiAutoOffEnabled;
    bool storedAutoMode = nvsAutoModeEnabled;
    bool storedTlsscBypass = nvsTlsscBypass;
    bool storedEvd = nvsEmergencyVehicleDetection;
    bool storedIsaChimeSuppress = nvsIsaChimeSuppress;
    bool storedIsaOverride = nvsIsaOverride;
    uint8_t storedHw4OffsetRaw = nvsHw4OffsetRaw;
    bool storedBanShield = nvsBanShieldEnable;
    int storedLegacyOffset = nvsLegacyOffset;
    bool storedRemoveVisionSpeedLimit = nvsRemoveVisionSpeedLimit;
    bool storedOverrideSpeedLimit = nvsOverrideSpeedLimit;
    bool h3Custom = hw3CustomSpeed;
    bool h3HighSpeed = hw3HighSpeedEnable;
    uint8_t h3Enc = hw3WireEncoding;
    uint8_t h3CustomTargets[kHw3CustomTargetCount];
    uint8_t h3HighSpeedTargets[kHw3HighSpeedBucketCount];
    bool storedLegacyMppOverride = legacyMppOverride;
    bool storedLegacyMppCustomEnable = legacyMppCustomEnable;
    bool storedLegacyMppHighSpeedEnable = legacyMppHighSpeedEnable;
    uint8_t storedLegacyMppCustomTargets[kLegacyMppCustomTargetCount];
    uint8_t storedLegacyMppHighSpeedTargets[kLegacyMppHighSpeedBucketCount];
    int canTx = -1, canRx = -1;

    for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
        h3CustomTargets[i] = hw3CustomTarget[i];
    for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
        h3HighSpeedTargets[i] = hw3HighSpeedTarget[i];
    for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
        storedLegacyMppCustomTargets[i] = legacyMppCustomTarget[i];
    for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
        storedLegacyMppHighSpeedTargets[i] = legacyMppHighSpeedTarget[i];

    if (p.begin(PREFS_NS, false))
    {
        storedHw = p.getUChar("hw", hwMode);
        storedCan = p.getBool("can", canActive);
        storedForce = p.getBool("force_act", forceActivate);
        storedBootCan = p.getBool("boot_can", bootCanActive);
        storedApGate = p.getBool("ap_gate", apInjectionGate);
        storedApRestore = p.getBool("ap_rst", apAutoRestore);
        spAuto = p.getBool("sp_auto", dashSpeedProfileAuto);
        spSel = p.getUChar("sp_sel", dashManualSpeedProfile);
        storedDriveProfile = p.getUChar("drv_prof", dashDriveProfile);
        storedSpeedStrategy = p.getUChar("spd_str", dashSpeedStrategy);
        storedOffsetMode = p.getUChar("offsetMode", offsetMode);
        storedManualPct = dashClampSpeedCustomPct(p.getUChar("manualPct", manualOffsetPct));
        storedCustomPct[0] = dashClampSpeedCustomPct(p.getUChar("cp0", customPct[0]));
        storedCustomPct[1] = dashClampSpeedCustomPct(p.getUChar("cp1", customPct[1]));
        storedCustomPct[2] = dashClampSpeedCustomPct(p.getUChar("cp2", customPct[2]));
        storedCustomPct[3] = dashClampSpeedCustomPct(p.getUChar("cp3", customPct[3]));
        storedLightingEnabled = p.getBool("lt_en", dashLightingEnabled);
        storedLightingCount = p.getUChar("lt_cnt", dashLightingCount);
        storedLightingFrequency = p.getUChar("lt_freq", dashLightingFrequency);
        storedRearFogStrategy = p.getUChar("lt_fog", dashRearFogStrategy);
        storedDefenseEnabled = p.getBool("def_en", dashDefenseEnabled);
        storedBionicSteering = p.getBool("def_bio", dashBionicSteering);
        storedNagTorqueTamper = p.getBool("def_ntt", dashNagTorqueTamper);
        storedSpeedNoDisturb = p.getBool("def_nd", dashSpeedNoDisturb);
        storedDndVolume = p.getBool("def_dv", dashDndVolume);
        storedDndSpeed = p.getBool("def_ds", dashDndSpeed);
        storedApEapCompatible = p.getBool("def_apeap", dashApEapCompatible);
        storedAutoShutdown = p.getBool(NVS_KEY_AUTO_SHUTDOWN, autoShutdownEnabled);
        storedWifiAutoOff = p.getBool(NVS_KEY_WIFI_AUTO_OFF, wifiAutoOffEnabled);
        storedAutoMode = p.getBool("fa", nvsAutoModeEnabled);
        storedTlsscBypass = p.getBool("fb", nvsTlsscBypass);
        storedEvd = p.getBool("fc", nvsEmergencyVehicleDetection);
        storedIsaChimeSuppress = p.getBool("fd", nvsIsaChimeSuppress);
        storedIsaOverride = p.getBool("fj", nvsIsaOverride);
        storedHw4OffsetRaw = p.getUChar("fe", nvsHw4OffsetRaw);
        storedBanShield = p.getBool("ff", nvsBanShieldEnable);
        storedLegacyOffset = (int)p.getUChar("fg", (uint8_t)(nvsLegacyOffset + 30)) - 30;
        storedRemoveVisionSpeedLimit = p.getBool("fh", nvsRemoveVisionSpeedLimit);
        storedOverrideSpeedLimit = p.getBool("fi", nvsOverrideSpeedLimit);
        eprn = p.getBool("eprn", true);
        if (p.isKey("ap_ssid"))
            apSsid = p.getString("ap_ssid", "");
        if (p.isKey("ap_pass"))
            apPass = p.getString("ap_pass", "");
        apHid = p.getBool("ap_hidden", false);
        if (p.isKey("wifi_ssid"))
            wSsid = p.getString("wifi_ssid", "");
        if (p.isKey("wifi_pass"))
            wPass = p.getString("wifi_pass", "");
        wStatic = p.getBool("wifi_static", false);
        if (p.isKey("wifi_ip"))
            wIp = p.getString("wifi_ip", "");
        if (p.isKey("wifi_gw"))
            wGw = p.getString("wifi_gw", "");
        if (p.isKey("wifi_mask"))
            wMask = p.getString("wifi_mask", "");
        if (p.isKey("wifi_dns"))
            wDns = p.getString("wifi_dns", "");
        beta = p.getBool("update_beta", p.getBool("upd_beta", false));
        autoUpdate = p.getBool("auto_upd", false);
        h3Slew = p.getBool("h3_slw", false);
        h3SlewRate = dashLoadHw3SlewRate(p.getUChar("h3_srt", kHw3SlewRateDefault));
        h3Custom = p.getBool("h3_cust", hw3CustomSpeed);
        h3HighSpeed = p.getBool("h3_hse", hw3HighSpeedEnable);
        h3Enc = p.getUChar("h3_enc", hw3WireEncoding);
        char k[8];
        for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
        {
            snprintf(k, sizeof(k), "h3_ct%u", (unsigned)i);
            h3CustomTargets[i] = p.getUChar(k, h3CustomTargets[i]);
        }
        for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
        {
            snprintf(k, sizeof(k), "h3_ht%u", (unsigned)i);
            h3HighSpeedTargets[i] = p.getUChar(k, h3HighSpeedTargets[i]);
        }
        storedLegacyMppOverride = p.getBool("lg_mpp_en", legacyMppOverride);
        storedLegacyMppCustomEnable = p.getBool("lg_mppc_en", legacyMppCustomEnable);
        storedLegacyMppHighSpeedEnable = p.getBool("lg_mpph_en", legacyMppHighSpeedEnable);
        for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
        {
            snprintf(k, sizeof(k), "lg_ct%u", (unsigned)i);
            storedLegacyMppCustomTargets[i] = p.getUChar(k, storedLegacyMppCustomTargets[i]);
        }
        for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
        {
            snprintf(k, sizeof(k), "lg_ht%u", (unsigned)i);
            storedLegacyMppHighSpeedTargets[i] = p.getUChar(k, storedLegacyMppHighSpeedTargets[i]);
        }
        p.end();
    }
    Preferences cp;
    if (cp.begin("can", true))
    {
        canTx = cp.getChar("tx", -1);
        canRx = cp.getChar("rx", -1);
        cp.end();
    }

    String j = "{\"version\":\"" FIRMWARE_VERSION "\"";
    j += ",\"device\":{\"hw\":" + String(storedHw) + ",\"can\":" + String(storedCan ? "true" : "false");
    j += ",\"force\":" + String(storedForce ? "true" : "false");
    j += ",\"bootCan\":" + String(storedBootCan ? "true" : "false");
    j += ",\"apGate\":" + String(storedApGate ? "true" : "false");
    j += ",\"apAutoRestore\":" + String(storedApRestore ? "true" : "false");
    j += ",\"speedProfileAuto\":" + String(spAuto ? "true" : "false") + ",\"speedProfile\":" + String(spSel);
    j += ",\"driveProfile\":" + String(storedDriveProfile);
    j += ",\"speedStrategy\":" + String(storedSpeedStrategy) + ",\"offsetMode\":" + String(storedOffsetMode);
    j += ",\"dashboardLog\":" + String(eprn ? "true" : "false") + "}";
    j += ",\"ap\":{\"ssid\":\"" + jsonEscape(apSsid) + "\",\"pass\":\"" + jsonEscape(apPass) + "\",\"hidden\":" + String(apHid ? "true" : "false") + "}";
    j += ",\"wifi\":{\"ssid\":\"" + jsonEscape(wSsid) + "\",\"pass\":\"" + jsonEscape(wPass) + "\"";
    j += ",\"static\":" + String(wStatic ? "true" : "false");
    j += ",\"ip\":\"" + jsonEscape(wIp) + "\",\"gw\":\"" + jsonEscape(wGw) + "\"";
    j += ",\"mask\":\"" + jsonEscape(wMask) + "\",\"dns\":\"" + jsonEscape(wDns) + "\"}";
    j += ",\"wifiNetworks\":[";
    for (uint8_t i = 0; i < wifiNetworkCount; i++)
    {
        if (i)
            j += ",";
        const DashWifiNetwork &n = wifiNetworks[i];
        j += "{\"ssid\":\"" + jsonEscape(n.ssid) + "\",\"pass\":\"" + jsonEscape(n.pass) + "\"";
        j += ",\"static\":" + String(n.useStatic ? "true" : "false");
        j += ",\"ip\":\"" + jsonEscape(n.ip) + "\",\"gw\":\"" + jsonEscape(n.gw) + "\"";
        j += ",\"mask\":\"" + jsonEscape(n.mask) + "\",\"dns\":\"" + jsonEscape(n.dns) + "\"}";
    }
    j += "]";
    j += ",\"wifiPreferred\":" + String(wifiActiveSlot >= 0 ? wifiActiveSlot : 0);
    j += ",\"hw3\":{\"offsetSlew\":" + String(h3Slew ? "true" : "false") + ",\"slewRate\":" + String(h3SlewRate);
    j += ",\"custom\":" + String(h3Custom ? "true" : "false");
    j += ",\"highSpeed\":" + String(h3HighSpeed ? "true" : "false") + ",\"encoding\":" + String(h3Enc);
    j += ",\"customTargets\":[";
    for (uint8_t i = 0; i < kHw3CustomTargetCount; i++)
    {
        if (i)
            j += ",";
        j += String(h3CustomTargets[i]);
    }
    j += "],\"highSpeedTargets\":[";
    for (uint8_t i = 0; i < kHw3HighSpeedBucketCount; i++)
    {
        if (i)
            j += ",";
        j += String(h3HighSpeedTargets[i]);
    }
    j += "]}";
    j += ",\"speed\":{\"manualPct\":" + String(storedManualPct) + ",\"customPct\":[";
    for (uint8_t i = 0; i < 4; i++)
    {
        if (i)
            j += ",";
        j += String(storedCustomPct[i]);
    }
    j += "]}";
    j += ",\"lighting\":{\"enabled\":" + String(storedLightingEnabled ? "true" : "false");
    j += ",\"count\":" + String(storedLightingCount) + ",\"frequencyValue\":" + String(storedLightingFrequency);
    j += ",\"rearFogValue\":" + String(storedRearFogStrategy) + "}";
    j += ",\"defense\":{\"enabled\":" + String(storedDefenseEnabled ? "true" : "false");
    j += ",\"bionicSteering\":" + String(storedBionicSteering ? "true" : "false");
    j += ",\"nagTorqueTamper\":" + String(storedNagTorqueTamper ? "true" : "false");
    j += ",\"speedNoDisturb\":" + String(storedSpeedNoDisturb ? "true" : "false");
    j += ",\"dndVolume\":" + String(storedDndVolume ? "true" : "false");
    j += ",\"dndSpeed\":" + String(storedDndSpeed ? "true" : "false");
    j += ",\"apEapCompatible\":" + String(storedApEapCompatible ? "true" : "false") + "}";
    j += ",\"power\":{\"autoShutdown\":" + String(storedAutoShutdown ? "true" : "false");
    j += ",\"wifiAutoOff\":" + String(storedWifiAutoOff ? "true" : "false") + "}";
    j += ",\"fsdRuntime\":{\"autoMode\":" + String(storedAutoMode ? "true" : "false");
    j += ",\"tlsscBypass\":" + String(storedTlsscBypass ? "true" : "false");
    j += ",\"evd\":" + String(storedEvd ? "true" : "false");
    j += ",\"isaChimeSuppress\":" + String(storedIsaChimeSuppress ? "true" : "false");
    j += ",\"isaOverride\":" + String(storedIsaOverride ? "true" : "false");
    j += ",\"hw4OffsetRaw\":" + String(storedHw4OffsetRaw);
    j += ",\"banShield\":" + String(storedBanShield ? "true" : "false");
    j += ",\"legacyOffset\":" + String(storedLegacyOffset);
    j += ",\"removeVisionSpeedLimit\":" + String(storedRemoveVisionSpeedLimit ? "true" : "false");
    j += ",\"overrideSpeedLimit\":" + String(storedOverrideSpeedLimit ? "true" : "false") + "}";
    j += ",\"legacyMpp\":{\"override\":" + String(storedLegacyMppOverride ? "true" : "false");
    j += ",\"customEnable\":" + String(storedLegacyMppCustomEnable ? "true" : "false");
    j += ",\"highSpeedEnable\":" + String(storedLegacyMppHighSpeedEnable ? "true" : "false");
    j += ",\"customTargets\":[";
    for (uint8_t i = 0; i < kLegacyMppCustomTargetCount; i++)
    {
        if (i)
            j += ",";
        j += String(storedLegacyMppCustomTargets[i]);
    }
    j += "],\"highSpeedTargets\":[";
    for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount; i++)
    {
        if (i)
            j += ",";
        j += String(storedLegacyMppHighSpeedTargets[i]);
    }
    j += "]}";
    j += ",\"can\":{\"tx\":" + String(canTx) + ",\"rx\":" + String(canRx) + "}";
    j += ",\"updates\":{\"beta\":" + String(beta ? "true" : "false") + ",\"auto\":" + String(autoUpdate ? "true" : "false") + "}";
    j += ",\"beta\":" + String(beta ? "true" : "false");
#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)
    j += ",\"gateway\":{\"enabled\":" + String(gatewayEnabled ? "true" : "false");
    j += ",\"blacklist\":\"" + jsonEscape(gatewayDnsBlacklist.c_str()) + "\"";
    j += ",\"whitelist\":\"" + jsonEscape(gatewayDnsWhitelist.c_str()) + "\"}";
#endif
    j += "}";

    server.sendHeader("Content-Disposition", "attachment; filename=\"evtools-backup.json\"");
    server.send(200, "application/json", j);
}

static void handleSettingsImport()
{
    String body = server.arg("plain");
    if (body.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    Preferences p;
    if (!p.begin(PREFS_NS, false))
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"NVS open failed\"}");
        return;
    }

    if (doc["device"].is<JsonObject>())
    {
        JsonObject device = doc["device"].as<JsonObject>();
        if (device["hw"].is<int>())
        {
            int hw = device["hw"].as<int>();
            if (hw >= 0 && hw <= 3)
                p.putUChar("hw", static_cast<uint8_t>(hw));
        }
        if (device["can"].is<bool>())
        {
            bool fsdSwitch = device["can"].as<bool>();
            p.putBool("can", fsdSwitch);
            p.putBool("force_act", fsdSwitch);
            if (!device["bootCan"].is<bool>())
                p.putBool("boot_can", fsdSwitch);
        }
        if (device["force"].is<bool>())
            p.putBool("force_act", device["force"].as<bool>());
        if (device["bootCan"].is<bool>())
            p.putBool("boot_can", device["bootCan"].as<bool>());
        if (device["apGate"].is<bool>())
            p.putBool("ap_gate", device["apGate"].as<bool>());
        if (device["apAutoRestore"].is<bool>())
            p.putBool("ap_rst", device["apAutoRestore"].as<bool>());
        if (device["speedProfileAuto"].is<bool>())
            p.putBool("sp_auto", device["speedProfileAuto"].as<bool>());
        if (device["speedProfile"].is<int>())
        {
            uint8_t targetHw = p.getUChar("hw", hwMode);
            p.putUChar("sp_sel", dashClampSpeedProfileForHw(targetHw, device["speedProfile"].as<int>()));
        }
        if (device["driveProfile"].is<int>())
        {
            int profile = device["driveProfile"].as<int>();
            if (profile >= 0 && profile <= 5)
                p.putUChar("drv_prof", static_cast<uint8_t>(profile));
        }
        int strategy = -1;
        if (device["speedStrategy"].is<int>())
            strategy = device["speedStrategy"].as<int>();
        else if (device["offsetMode"].is<int>())
            strategy = device["offsetMode"].as<int>();
        if (strategy >= 0 && strategy <= 2)
        {
            p.putUChar("spd_str", static_cast<uint8_t>(strategy));
            p.putUChar("offsetMode", static_cast<uint8_t>(strategy));
        }
        if (device["dashboardLog"].is<bool>())
            p.putBool("eprn", device["dashboardLog"].as<bool>());
    }
    if (doc["ap"].is<JsonObject>())
    {
        const char *s = doc["ap"]["ssid"] | "";
        const char *pw = doc["ap"]["pass"] | "";
        size_t ssidLen = strlen(s);
        size_t passLen = strlen(pw);
        if (ssidLen > 0 && ssidLen <= kDashMaxSsidLen)
            p.putString("ap_ssid", s);
        if (dashApPasswordLengthValid(passLen))
            p.putString("ap_pass", pw);
        if (doc["ap"]["hidden"].is<bool>())
            p.putBool("ap_hidden", doc["ap"]["hidden"].as<bool>());
    }
    if (doc["wifi"].is<JsonObject>())
    {
        const char *s = doc["wifi"]["ssid"] | "";
        const char *pw = doc["wifi"]["pass"] | "";
        if (strlen(s) <= kDashMaxSsidLen && strlen(pw) <= kDashMaxPassLen)
        {
            p.putString("wifi_ssid", s);
            p.putString("wifi_pass", pw);
        }
        p.putBool("wifi_static", doc["wifi"]["static"] | false);
        p.putString("wifi_ip", (const char *)(doc["wifi"]["ip"] | ""));
        p.putString("wifi_gw", (const char *)(doc["wifi"]["gw"] | ""));
        p.putString("wifi_mask", (const char *)(doc["wifi"]["mask"] | ""));
        p.putString("wifi_dns", (const char *)(doc["wifi"]["dns"] | ""));
    }
    if (doc["wifiNetworks"].is<JsonArray>())
    {
        JsonArray nets = doc["wifiNetworks"].as<JsonArray>();
        uint8_t count = 0;
        for (uint8_t i = 0; i < kDashMaxWifiNetworks; i++)
            dashRemoveWifiSlotKeys(i);
        for (JsonVariant v : nets)
        {
            if (count >= kDashMaxWifiNetworks || !v.is<JsonObject>())
                break;
            const char *s = v["ssid"] | "";
            const char *pw = v["pass"] | "";
            if (strlen(s) == 0 || !dashStaConfigLengthValid(String(s), String(pw)) || dashStaSsidLooksCorrupt(String(s)))
                continue;
            p.putString(dashWifiKey(count, "s").c_str(), s);
            p.putString(dashWifiKey(count, "p").c_str(), pw);
            bool st = v["static"] | false;
            p.putBool(dashWifiKey(count, "t").c_str(), st);
            if (st)
            {
                p.putString(dashWifiKey(count, "i").c_str(), (const char *)(v["ip"] | "0.0.0.0"));
                p.putString(dashWifiKey(count, "g").c_str(), (const char *)(v["gw"] | "0.0.0.0"));
                p.putString(dashWifiKey(count, "m").c_str(), (const char *)(v["mask"] | "255.255.255.0"));
                p.putString(dashWifiKey(count, "d").c_str(), (const char *)(v["dns"] | "0.0.0.0"));
            }
            count++;
        }
        p.putUChar("wn_cnt", count);
        uint8_t pref = doc["wifiPreferred"] | 0;
        p.putUChar("wn_pref", count > 0 && pref < count ? pref : 0);
    }
    if (doc["updates"].is<JsonObject>())
    {
        if (doc["updates"]["beta"].is<bool>())
        {
            p.putBool("update_beta", doc["updates"]["beta"].as<bool>());
            p.putBool("upd_beta", doc["updates"]["beta"].as<bool>());
        }
        if (doc["updates"]["auto"].is<bool>())
            p.putBool("auto_upd", doc["updates"]["auto"].as<bool>());
    }
    else if (doc["beta"].is<bool>())
    {
        p.putBool("update_beta", doc["beta"].as<bool>());
        p.putBool("upd_beta", doc["beta"].as<bool>());
    }
    if (doc["hw3"].is<JsonObject>())
    {
        if (doc["hw3"]["offsetSlew"].is<bool>())
            p.putBool("h3_slw", doc["hw3"]["offsetSlew"].as<bool>());
        if (doc["hw3"]["slewRate"].is<int>())
            p.putUChar("h3_srt", dashClampHw3SlewRate(doc["hw3"]["slewRate"].as<int>()));
        if (doc["hw3"]["custom"].is<bool>())
            p.putBool("h3_cust", doc["hw3"]["custom"].as<bool>());
        if (doc["hw3"]["highSpeed"].is<bool>())
            p.putBool("h3_hse", doc["hw3"]["highSpeed"].as<bool>());
        if (doc["hw3"]["encoding"].is<int>())
        {
            uint8_t enc = doc["hw3"]["encoding"].as<int>() == kHw3WireEncKph5 ? kHw3WireEncKph5 : kHw3WireEncPct4;
            p.putUChar("h3_enc", enc);
        }
        if (doc["hw3"]["customTargets"].is<JsonArray>())
        {
            JsonArray arr = doc["hw3"]["customTargets"].as<JsonArray>();
            char k[8];
            for (uint8_t i = 0; i < kHw3CustomTargetCount && i < arr.size(); i++)
            {
                snprintf(k, sizeof(k), "h3_ct%u", (unsigned)i);
                p.putUChar(k, dashClampHw3CustomTargetForBucket(i, arr[i].as<int>()));
            }
        }
        if (doc["hw3"]["highSpeedTargets"].is<JsonArray>())
        {
            JsonArray arr = doc["hw3"]["highSpeedTargets"].as<JsonArray>();
            char k[8];
            for (uint8_t i = 0; i < kHw3HighSpeedBucketCount && i < arr.size(); i++)
            {
                snprintf(k, sizeof(k), "h3_ht%u", (unsigned)i);
                p.putUChar(k, dashClampHw3HighSpeedTargetForBucket(i, arr[i].as<int>()));
            }
        }
    }
    if (doc["speed"].is<JsonObject>())
    {
        JsonObject speed = doc["speed"].as<JsonObject>();
        if (speed["manualPct"].is<int>())
            p.putUChar("manualPct", dashClampSpeedCustomPct(speed["manualPct"].as<int>()));
        if (speed["customPct"].is<JsonArray>())
        {
            JsonArray arr = speed["customPct"].as<JsonArray>();
            char k[8];
            for (uint8_t i = 0; i < 4 && i < arr.size(); i++)
            {
                snprintf(k, sizeof(k), "cp%u", (unsigned)i);
                p.putUChar(k, dashClampSpeedCustomPct(arr[i].as<int>()));
            }
        }
    }
    if (doc["lighting"].is<JsonObject>())
    {
        JsonObject lighting = doc["lighting"].as<JsonObject>();
        if (lighting["enabled"].is<bool>())
            p.putBool("lt_en", lighting["enabled"].as<bool>());
        if (lighting["count"].is<int>())
        {
            uint8_t count = lighting["count"].as<int>();
            if (count == 3 || count == 5 || count == 7 || count == 10)
                p.putUChar("lt_cnt", count);
        }
        if (lighting["frequencyValue"].is<int>())
        {
            uint8_t frequency = lighting["frequencyValue"].as<int>();
            if (frequency <= 2)
                p.putUChar("lt_freq", frequency);
        }
        if (lighting["rearFogValue"].is<int>())
        {
            uint8_t strategy = lighting["rearFogValue"].as<int>();
            if (strategy <= 2)
                p.putUChar("lt_fog", strategy);
        }
    }
    if (doc["defense"].is<JsonObject>())
    {
        JsonObject defense = doc["defense"].as<JsonObject>();
        if (defense["enabled"].is<bool>())
            p.putBool("def_en", defense["enabled"].as<bool>());
        if (defense["bionicSteering"].is<bool>())
            p.putBool("def_bio", defense["bionicSteering"].as<bool>());
        if (defense["nagTorqueTamper"].is<bool>())
            p.putBool("def_ntt", defense["nagTorqueTamper"].as<bool>());
        if (defense["speedNoDisturb"].is<bool>())
            p.putBool("def_nd", defense["speedNoDisturb"].as<bool>());
        if (defense["dndVolume"].is<bool>())
            p.putBool("def_dv", defense["dndVolume"].as<bool>());
        if (defense["dndSpeed"].is<bool>())
            p.putBool("def_ds", defense["dndSpeed"].as<bool>());
        if (defense["apEapCompatible"].is<bool>())
            p.putBool("def_apeap", defense["apEapCompatible"].as<bool>());
    }
    if (doc["power"].is<JsonObject>())
    {
        JsonObject power = doc["power"].as<JsonObject>();
        if (power["autoShutdown"].is<bool>())
            p.putBool(NVS_KEY_AUTO_SHUTDOWN, power["autoShutdown"].as<bool>());
        if (power["wifiAutoOff"].is<bool>())
            p.putBool(NVS_KEY_WIFI_AUTO_OFF, power["wifiAutoOff"].as<bool>());
    }
    if (doc["fsdRuntime"].is<JsonObject>())
    {
        JsonObject fsd = doc["fsdRuntime"].as<JsonObject>();
        if (fsd["autoMode"].is<bool>())
            p.putBool("fa", fsd["autoMode"].as<bool>());
        if (fsd["tlsscBypass"].is<bool>())
            p.putBool("fb", fsd["tlsscBypass"].as<bool>());
        if (fsd["evd"].is<bool>())
            p.putBool("fc", fsd["evd"].as<bool>());
        if (fsd["isaChimeSuppress"].is<bool>())
            p.putBool("fd", fsd["isaChimeSuppress"].as<bool>());
        if (fsd["isaOverride"].is<bool>())
            p.putBool("fj", fsd["isaOverride"].as<bool>());
        if (fsd["hw4OffsetRaw"].is<int>())
        {
            int raw = fsd["hw4OffsetRaw"].as<int>();
            if (raw >= 0 && raw <= 255)
                p.putUChar("fe", static_cast<uint8_t>(raw));
        }
        if (fsd["banShield"].is<bool>())
            p.putBool("ff", fsd["banShield"].as<bool>());
        if (fsd["legacyOffset"].is<int>())
        {
            int offset = fsd["legacyOffset"].as<int>();
            if (offset < -30)
                offset = -30;
            if (offset > 225)
                offset = 225;
            p.putUChar("fg", static_cast<uint8_t>(offset + 30));
        }
        if (fsd["removeVisionSpeedLimit"].is<bool>())
            p.putBool("fh", fsd["removeVisionSpeedLimit"].as<bool>());
        if (fsd["overrideSpeedLimit"].is<bool>())
            p.putBool("fi", fsd["overrideSpeedLimit"].as<bool>());
    }
    if (doc["legacyMpp"].is<JsonObject>())
    {
        JsonObject legacy = doc["legacyMpp"].as<JsonObject>();
        if (legacy["override"].is<bool>())
            p.putBool("lg_mpp_en", legacy["override"].as<bool>());
        if (legacy["customEnable"].is<bool>())
            p.putBool("lg_mppc_en", legacy["customEnable"].as<bool>());
        if (legacy["highSpeedEnable"].is<bool>())
            p.putBool("lg_mpph_en", legacy["highSpeedEnable"].as<bool>());
        if (legacy["customTargets"].is<JsonArray>())
        {
            JsonArray arr = legacy["customTargets"].as<JsonArray>();
            char k[8];
            for (uint8_t i = 0; i < kLegacyMppCustomTargetCount && i < arr.size(); i++)
            {
                snprintf(k, sizeof(k), "lg_ct%u", (unsigned)i);
                p.putUChar(k, dashClampLegacyMppCustomTargetForBucket(i, arr[i].as<int>()));
            }
        }
        if (legacy["highSpeedTargets"].is<JsonArray>())
        {
            JsonArray arr = legacy["highSpeedTargets"].as<JsonArray>();
            char k[8];
            for (uint8_t i = 0; i < kLegacyMppHighSpeedBucketCount && i < arr.size(); i++)
            {
                snprintf(k, sizeof(k), "lg_ht%u", (unsigned)i);
                p.putUChar(k, dashClampLegacyMppHighSpeedTargetForBucket(i, arr[i].as<int>()));
            }
        }
    }
    p.end();

    if (doc["can"].is<JsonObject>())
    {
        int tx = doc["can"]["tx"] | -1;
        int rx = doc["can"]["rx"] | -1;
        Preferences cp;
        if (cp.begin("can", false))
        {
            if (tx >= 0 && tx <= 39 && rx >= 0 && rx <= 39 && tx != rx &&
                !((tx >= 6 && tx <= 11) || (rx >= 6 && rx <= 11)))
            {
                cp.putChar("tx", (int8_t)tx);
                cp.putChar("rx", (int8_t)rx);
            }
            cp.end();
        }
    }

#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)
    if (doc["gateway"].is<JsonObject>())
    {
        JsonObject gw = doc["gateway"].as<JsonObject>();
        if (gw["enabled"].is<bool>())
            gatewayEnabled = gw["enabled"].as<bool>();
        if (gw["blacklist"].is<const char *>())
            gatewayDnsBlacklist = dashGatewaySanitizeBlacklist((const char *)(gw["blacklist"] | ""));
        if (gw["whitelist"].is<const char *>())
            gatewayDnsWhitelist = dashGatewaySanitizeWhitelist((const char *)(gw["whitelist"] | ""));
        dashGatewaySave();
    }
#endif

    dashLog("[BACKUP] Settings imported (reboot required)");
    server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
}

static void handleApConfig()
{
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    bool hasHidden = server.hasArg("hidden");
    bool newHidden = hasHidden && (server.arg("hidden") == "1" || server.arg("hidden") == "true");

    if (newSsid.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID required\"}");
        return;
    }
    if (newSsid.length() > kDashMaxSsidLen)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID must be 32 bytes or less\"}");
        return;
    }
    if (newPass.length() > 0 && !dashApPasswordLengthValid(newPass.length()))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Password must be 8-64 characters\"}");
        return;
    }

    strlcpy(apSSID, newSsid.c_str(), sizeof(apSSID));
    if (newPass.length() > 0)
        strlcpy(apPass, newPass.c_str(), sizeof(apPass));
    if (hasHidden)
        apHidden = newHidden;

    prefs.begin(PREFS_NS, false);
    prefs.putString("ap_ssid", newSsid);
    if (newPass.length() > 0)
        prefs.putString("ap_pass", newPass);
    if (hasHidden)
        prefs.putBool("ap_hidden", newHidden);
    prefs.end();

    dashLog("[WIFI] AP config updated: SSID=" + newSsid + (apHidden ? " (hidden)" : "") +
            " channel=auto match STA");
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved. AP starts on CH1 and auto matches STA after WiFi connects.\"}");
}

static void handleApStatus()
{
    Preferences p;
    bool stored = false;
    if (p.begin(PREFS_NS, false))
    {
        stored = p.isKey("ap_ssid") && p.getString("ap_ssid", "").length() > 0;
        p.end();
    }
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    const char *wifiModeText = "off";
    switch (wifiMode)
    {
    case WIFI_MODE_STA:
        wifiModeText = "STA";
        break;
    case WIFI_MODE_AP:
        wifiModeText = "AP";
        break;
    case WIFI_MODE_APSTA:
        wifiModeText = "AP+STA";
        break;
    default:
        break;
    }
    String j = "{\"ssid\":\"" + jsonEscape(apSSID) + "\"";
    j += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
    j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
    j += ",\"mode\":\"" + String(wifiModeText) + "\"";
    j += ",\"channel\":" + String(dashCurrentApChannel());
    j += ",\"channel_auto\":true";
    j += ",\"last_channel_sync_ms\":" + String(apLastChannelSyncMs);
    j += ",\"last_channel_sync_target\":" + String(apLastChannelSyncTarget);
    j += ",\"last_channel_sync_ok\":" + String(apLastChannelSyncOk ? "true" : "false");
    j += ",\"stored\":" + String(stored ? "true" : "false");
    j += ",\"hidden\":" + String(apHidden ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

// ── OTA GitHub Update ───────────────────────────────────────────

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

#ifndef DASH_GITHUB_REPO
#define DASH_GITHUB_REPO "JordanzhaoD/waveshare-single-can-firmware"
#endif

static const char *GITHUB_REPO = DASH_GITHUB_REPO;

// Map driver type to release artifact filename
static const char *getFirmwareArtifact()
{
#if defined(DRIVER_T2CAN_DUAL)
    return "firmware-lilygo-t2can-dual.bin";
#elif defined(DRIVER_ESP32_EXT_MCP2515)
    return "firmware-esp32-ext-mcp2515.bin";
#elif defined(DASH_SINGLE_CAN_STANDALONE)
    return "firmware-waveshare-single-can.bin";
#else
    return "firmware-esp32.bin";
#endif
}

// Parse a semver-ish version string into (major, minor, patch, preRank, preNum).
// Pre-release rank: 0 = stable (no suffix, sorts highest among same M.m.p),
//                  1 = -alpha.N, 2 = -beta.N, 3 = -rc.N (higher rank = closer to stable).
// Unknown suffix → treated as stable (rank 0).
static void parseVersion(const String &v, int &maj, int &min, int &pat, int &preRank, int &preNum)
{
    maj = min = pat = 0;
    preRank = 0;
    preNum = 0;
    int i = 0;
    int len = v.length();
    auto readInt = [&](int &out)
    {
        int val = 0;
        bool any = false;
        while (i < len && v[i] >= '0' && v[i] <= '9')
        {
            val = val * 10 + (v[i] - '0');
            i++;
            any = true;
        }
        if (any)
            out = val;
    };
    readInt(maj);
    if (i < len && v[i] == '.')
    {
        i++;
        readInt(min);
    }
    if (i < len && v[i] == '.')
    {
        i++;
        readInt(pat);
    }
    if (i < len && v[i] == '-')
    {
        i++;
        String tail = v.substring(i);
        tail.toLowerCase();
        if (tail.startsWith("alpha"))
            preRank = 1;
        else if (tail.startsWith("beta"))
            preRank = 2;
        else if (tail.startsWith("rc"))
            preRank = 3;
        else
            preRank = 0; // unknown → treat as stable
        int dot = tail.indexOf('.');
        if (dot >= 0)
            preNum = tail.substring(dot + 1).toInt();
    }
}

// Returns true iff `candidate` is strictly newer than `current`.
static bool isVersionNewer(const String &candidate, const String &current)
{
    int cM, cm, cp, cR, cN;
    int uM, um, up, uR, uN;
    parseVersion(candidate, cM, cm, cp, cR, cN);
    parseVersion(current, uM, um, up, uR, uN);
    if (cM != uM)
        return cM > uM;
    if (cm != um)
        return cm > um;
    if (cp != up)
        return cp > up;
    // Same M.m.p — stable (rank 0) beats any prerelease (rank 1-3)
    // For two prereleases: higher rank beats lower (rc > beta > alpha)
    int cEff = (cR == 0) ? 1000 : cR; // stable → very high
    int uEff = (uR == 0) ? 1000 : uR;
    if (cEff != uEff)
        return cEff > uEff;
    return cN > uN;
}

static void handleUpdateCheck()
{
    if (!staConnected)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected\"}");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url;
    if (updateBetaChannel)
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases?per_page=1";
    else
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";

    http.begin(client, url);
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-OTA");
    int code = http.GET();

    if (code != 200)
    {
        http.end();
        String msg = code <= 0
                         ? "GitHub unreachable from ESP32. Use manual firmware upload."
                         : "GitHub API error " + String(code);
        server.send(502, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscape(msg.c_str()) + "\"}");
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"JSON parse error\"}");
        return;
    }

    // Find the right release
    JsonObject release;
    if (updateBetaChannel)
    {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject r : arr)
        {
            release = r;
            break; // first (newest) release
        }
    }
    else
    {
        release = doc.as<JsonObject>();
    }

    if (release.isNull())
    {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"No release found\"}");
        return;
    }

    String tagName = release["tag_name"] | "";
    bool prerelease = release["prerelease"] | false;
    String version = tagName;
    if (version.startsWith("v"))
        version = version.substring(1);

    // Find the matching firmware asset
    String downloadUrl = "";
    const char *artifact = getFirmwareArtifact();
    JsonArray assets = release["assets"];
    for (JsonObject asset : assets)
    {
        String name = asset["name"] | "";
        if (name == artifact)
        {
            downloadUrl = String(asset["browser_download_url"] | "");
            break;
        }
    }

    String j = "{\"ok\":true";
    j += ",\"current\":\"" + jsonEscape(FIRMWARE_VERSION) + "\"";
    j += ",\"latest\":\"" + jsonEscape(version.c_str()) + "\"";
    j += ",\"tag\":\"" + jsonEscape(tagName.c_str()) + "\"";
    j += ",\"prerelease\":" + String(prerelease ? "true" : "false");
    j += ",\"artifact\":\"" + jsonEscape(artifact) + "\"";
    j += ",\"url\":\"" + jsonEscape(downloadUrl.c_str()) + "\"";
    bool isNewer = isVersionNewer(version, String(FIRMWARE_VERSION));
    j += ",\"update\":" + String(isNewer && downloadUrl.length() > 0 ? "true" : "false");
    j += ",\"beta\":" + String(updateBetaChannel ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
}

static void handleUpdateInstall()
{
    if (!staConnected)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected\"}");
        return;
    }

    String url = server.arg("url");
    if (url.length() == 0)
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"No URL provided\"}");
        return;
    }

    dashLog("[OTA] Starting GitHub update from: " + url);
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Downloading and installing... Device will reboot.\"}");
    delay(500);

    WiFiClientSecure client;
    client.setInsecure();

    // Follow redirects — GitHub release assets redirect to S3
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, url);
    http.setTimeout(30000);
    http.addHeader("Accept", "application/octet-stream");
    int code = http.GET();

    if (code != 200)
    {
        dashLog("[OTA] Download failed: HTTP " + String(code));
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0)
    {
        dashLog("[OTA] Invalid content length: " + String(contentLength));
        http.end();
        return;
    }

    dashLog("[OTA] Downloading " + String(contentLength) + " bytes...");

    if (!Update.begin(contentLength))
    {
        dashLog("[OTA] Update.begin failed: " + String(Update.errorString()));
        http.end();
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != (size_t)contentLength)
    {
        dashLog("[OTA] Written " + String(written) + " of " + String(contentLength) + " bytes: " + String(Update.errorString()));
        Update.abort();
        return;
    }

    if (!Update.end(true))
    {
        dashLog("[OTA] Update finalize failed: " + String(Update.errorString()));
        return;
    }

    if (!Update.isFinished())
    {
        dashLog("[OTA] Update not finished");
        return;
    }

    dashLog("[OTA] Update successful! Rebooting...");
    delay(1000);
    ESP.restart();
}

// Check GitHub for a newer release and, if found, download + install it.
// Blocking; on success calls ESP.restart() and never returns.
static void performAutoUpdate()
{
    if (!staConnected)
        return;

    dashLog("[AUTO-OTA] Checking for updates...");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url;
    if (updateBetaChannel)
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases?per_page=1";
    else
        url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";

    http.begin(client, url);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-OTA");
    int code = http.GET();
    if (code != 200)
    {
        dashLog("[AUTO-OTA] GitHub API error " + String(code));
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload))
    {
        dashLog("[AUTO-OTA] JSON parse error");
        return;
    }

    JsonObject release;
    if (updateBetaChannel)
    {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject r : arr)
        {
            release = r;
            break;
        }
    }
    else
    {
        release = doc.as<JsonObject>();
    }
    if (release.isNull())
    {
        dashLog("[AUTO-OTA] No release found");
        return;
    }

    String tagName = release["tag_name"] | "";
    String version = tagName;
    if (version.startsWith("v"))
        version = version.substring(1);
    if (!isVersionNewer(version, String(FIRMWARE_VERSION)))
    {
        dashLog("[AUTO-OTA] No newer release (latest=" + version + ", current=" FIRMWARE_VERSION ")");
        return;
    }

    const char *artifact = getFirmwareArtifact();
    String downloadUrl = "";
    for (JsonObject asset : release["assets"].as<JsonArray>())
    {
        String name = asset["name"] | "";
        if (name == artifact)
        {
            downloadUrl = String(asset["browser_download_url"] | "");
            break;
        }
    }
    if (!downloadUrl.length())
    {
        dashLog("[AUTO-OTA] No matching artifact for this build");
        return;
    }

    dashLog("[AUTO-OTA] Update " + version + " available. Installing...");

    HTTPClient http2;
    http2.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http2.begin(client, downloadUrl);
    http2.addHeader("Accept", "application/octet-stream");
    int code2 = http2.GET();
    if (code2 != 200)
    {
        dashLog("[AUTO-OTA] Download failed: HTTP " + String(code2));
        http2.end();
        return;
    }
    int len = http2.getSize();
    if (len <= 0)
    {
        dashLog("[AUTO-OTA] Invalid content length: " + String(len));
        http2.end();
        return;
    }
    if (!Update.begin(len))
    {
        dashLog("[AUTO-OTA] Update.begin failed: " + String(Update.errorString()));
        http2.end();
        return;
    }
    WiFiClient *stream = http2.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http2.end();
    if (written != (size_t)len)
    {
        dashLog("[AUTO-OTA] Written " + String(written) + "/" + String(len) + " bytes: " + String(Update.errorString()));
        Update.abort();
        return;
    }
    if (!Update.end(true))
    {
        dashLog("[AUTO-OTA] Finalize failed: " + String(Update.errorString()));
        return;
    }
    dashLog("[AUTO-OTA] Update successful! Rebooting...");
    delay(1000);
    ESP.restart();
}

static void handleAutoUpdate()
{
    if (server.hasArg("enabled"))
    {
        autoUpdateEnabled = server.arg("enabled") == "1";
        prefs.begin(PREFS_NS, false);
        prefs.putBool("auto_upd", autoUpdateEnabled);
        prefs.end();
        dashLog("[AUTO-OTA] " + String(autoUpdateEnabled ? "enabled" : "disabled"));
    }
    String j = "{\"ok\":true,\"enabled\":";
    j += autoUpdateEnabled ? "true" : "false";
    j += "}";
    server.send(200, "application/json", j);
}

static void handleUpdateBeta()
{
    if (server.hasArg("beta"))
    {
        updateBetaChannel = server.arg("beta") == "1";
        prefs.begin(PREFS_NS, false);
        prefs.putBool("update_beta", updateBetaChannel);
        prefs.end();
        dashLog("[OTA] Channel: " + String(updateBetaChannel ? "beta" : "stable"));
    }
    String j = "{\"ok\":true,\"beta\":" + String(updateBetaChannel ? "true" : "false");
    j += ",\"version\":\"" + jsonEscape(FIRMWARE_VERSION) + "\"}";
    server.send(200, "application/json", j);
}

// Dashboard frame callback wrapper

static void webTask(void *)
{
    for (;;)
    {
        ArduinoOTA.handle();
        server.handleClient();
        dashCheckWifi();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static CarManagerBase *handlerPool[3] = {};

static void dashInitHandlers()
{
    handlerPool[0] = new LegacyHandler();
    handlerPool[1] = new HW3Handler();
    handlerPool[2] = new HW4Handler();
    for (int i = 0; i < 3; i++)
    {
        handlerPool[i]->onFrame = mcpDashOnFrame;
        handlerPool[i]->gateBlockReason = dashCurrentGateBlockReason;
        handlerPool[i]->legacyFsdActivationAllowed = dashLegacyFsdActivationAllowed;
        handlerPool[i]->pluginOwnsFsdActivation = dashPluginOwnsFsdActivation;
    }
}

static void dashSwapHandler(uint8_t mode)
{
    // mode=3 (AUTO): use DASH_DEFAULT_HW until CAN 920 detection kicks in.
    uint8_t effective = (mode == 3) ? DASH_DEFAULT_HW : mode;
    if (effective > 2 || !handlerPool[effective])
        return;
    CarManagerBase *next = handlerPool[effective];
    if (dashHandler)
        next->enablePrint = (bool)dashHandler->enablePrint;
    appActiveHandler = next;
    dashHandler = next;
    dashApplyRuntimeState();
    // Update driver acceptance filters for the new handler.
    // For MCP2515 (ext) dashApplyFilters() will also fine-tune the hardware
    // filter registers. For TWAI and old MCP2515 this abstract call is enough.
    if (dashDriver)
        dashDriver->setFilters(next->filterIds(), next->filterIdCount());
    const char *hwName = "LEGACY";
    if (mode == 1 || (mode == 3 && effective == 1))
        hwName = "HW3";
    else if (mode == 2 || (mode == 3 && effective == 2))
        hwName = "HW4";
    if (mode == 3)
        dashLog(String("[CFG] Handler AUTO -> ") + hwName);
    else
        dashLog("[CFG] Handler switched to " + String(hwName));
}

// Apply NVS-staged runtime switches to all handlerPool entries.
// Called once after dashInitHandlers() + dashSwapHandler().
static void dashApplyNvsRuntimeSwitches()
{
    for (int i = 0; i < 3; i++)
    {
        if (!handlerPool[i])
            continue;
        handlerPool[i]->autoModeEnabled = nvsAutoModeEnabled;
        handlerPool[i]->tlsscBypass = nvsTlsscBypass;
        handlerPool[i]->emergencyVehicleDetection = nvsEmergencyVehicleDetection;
        handlerPool[i]->isaChimeSuppress = nvsIsaChimeSuppress;
        handlerPool[i]->isaOverride = nvsIsaOverride;
        handlerPool[i]->hw4OffsetRaw = nvsHw4OffsetRaw;
        handlerPool[i]->banShieldEnable = nvsBanShieldEnable;
        handlerPool[i]->bionicSteering = dashBionicSteering;
        handlerPool[i]->legacyOffset = nvsLegacyOffset;
        handlerPool[i]->removeVisionSpeedLimit = nvsRemoveVisionSpeedLimit;
        handlerPool[i]->overrideSpeedLimit = nvsOverrideSpeedLimit;
        handlerPool[i]->legacyFsdDiag.policy = dashLegacyFsdPolicy;
        handlerPool[i]->legacyFsdDiag.mux1Enable = dashLegacyFsdMux1Enable;
        handlerPool[i]->legacyFsdDiag.profileWriteEnable = dashLegacyFsdProfileWriteEnable;
        handlerPool[i]->legacyFsdDiag.visionLimitClearEnable = dashLegacyFsdVisionLimitClearEnable;
    }
}

#if defined(DRIVER_ESP32_EXT_MCP2515)
static void mcpDashboardSetup(CarManagerBase *handler, CanDriver *driver, MCP2515 *mcp)
{
    dashHandler = handler;
    dashDriver = driver;
    dashMcp = mcp;
#else
static void mcpDashboardSetup(CarManagerBase *handler, CanDriver *driver)
{
    dashHandler = handler;
    dashDriver = driver;
#endif
    if (dashDriver)
        dashDriver->onSendFrame = mcpDashOnTxFrame;
    startMs = millis();
    fpsLastMs = millis();
    dashResetWriteProbe();

    if (!SPIFFS.begin(true))
        dashLog("[WARN] SPIFFS mount failed");

    dashLoadPrefs();
    dashGatewayLoad();
#if defined(DASH_PLUGIN_ENGINE)
    // Restore installed plugins + replay count from SPIFFS before handlers
    // register so the first /plugins status reflects persisted state.
    dashLoadPluginState();
#endif
    // Always boot in AP+STA mode so the STA interface is ready immediately.
    // This prevents connection failures to saved networks caused by late mode switching.
    dashStartAccessPoint(true);
    if (apHidden)
        dashLog("[WIFI] AP SSID is hidden");
    Serial.printf("[WIFI] AP: %s  IP: %s\n", apSSID, WiFi.softAPIP().toString().c_str());

    dashInitHandlers();
    dashApplyNvsRuntimeSwitches();
    dashSwapHandler(hwMode);
    dashApplyRuntimeState();
    dashApplyFilters();

    // Phase 3: Bionic PRNG uses default seed 0xDEADBEEF.
    // For better entropy, re-seed from millis() at boot via appActiveHandler.
    // NOTE: NagHandler's bionic path was removed (dual-mode refactor, 2026-06-24);
    // DashBionicSteer is retained (standalone-tested) but currently unused in production.

    // Phase 1: 初始化功耗管理
    dashPowerMgmtInit();

    ArduinoOTA.setHostname("ev-open-can-tools");
    ArduinoOTA.setPassword(DASH_OTA_PASS);
    ArduinoOTA.onStart([]()
                       { dashLog("[OTA] Starting..."); });
    ArduinoOTA.onEnd([]()
                     { dashLog("[OTA] Done -- rebooting"); });
    ArduinoOTA.onError([](ota_error_t e)
                       { dashLog("[OTA] Error: " + String(e)); });
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/config", HTTP_GET, handleConfigGet);
    server.on("/config", HTTP_POST, handleConfig);
    server.on("/mode_hw", HTTP_GET, handleModeHw);
    server.on("/mode_hw", HTTP_POST, handleModeHw);
    server.on("/drive_profile", HTTP_GET, handleDriveProfile);
    server.on("/drive_profile", HTTP_POST, handleDriveProfile);
    server.on("/speed_strategy", HTTP_GET, handleSpeedStrategy);
    server.on("/speed_strategy", HTTP_POST, handleSpeedStrategy);
    server.on("/speed_custom", HTTP_GET, handleSpeedCustomGet);
    server.on("/speed_custom", HTTP_POST, handleSpeedCustom);
#if !defined(DASH_SINGLE_CAN_STANDALONE)
    server.on("/lighting_config", HTTP_GET, handleLightingConfig);
    server.on("/lighting_config", HTTP_POST, handleLightingConfig);
#endif
    server.on("/defense_config", HTTP_GET, handleDefenseConfig);
    server.on("/defense_config", HTTP_POST, handleDefenseConfig);
    server.on("/legacy_fsd_config", HTTP_GET, handleLegacyFsdConfig);
    server.on("/legacy_fsd_config", HTTP_POST, handleLegacyFsdConfig);
    server.on("/gear_assist_status", HTTP_GET, handleGearAssistStatus);
    // Phase 1 新增端点
    server.on("/power_mgmt", HTTP_GET, handlePowerMgmt);
    server.on("/power_mgmt", HTTP_POST, handlePowerMgmt);
    server.on("/vehicle_ota_status", HTTP_GET, handleVehicleOtaStatus);
#if !defined(DASH_SINGLE_CAN_STANDALONE)
    server.on("/fog_light", HTTP_GET, handleFogLight);
    server.on("/fog_light", HTTP_POST, handleFogLight);
    server.on("/strobe_cont", HTTP_POST, handleStrobeCont);
#endif
    server.on("/hotspot_config", HTTP_GET, handleHotspotConfig);
    server.on("/hotspot_config", HTTP_POST, handleHotspotConfig);
    server.on("/relay_wifi_test", HTTP_POST, handleRelayWifiTest);
    server.on("/dns_rules", HTTP_GET, handleDnsRules);
    server.on("/dns_rules", HTTP_POST, handleDnsRules);
    server.on("/logging", HTTP_POST, handleLoggingConfig);
    server.on("/frames", HTTP_GET, handleFrames);
    server.on("/log", HTTP_GET, handleLog);
    server.on("/reset_stats", HTTP_GET, handleResetStats);
    server.on("/reset_stats", HTTP_POST, handleResetStats);
    server.on("/rec_start", HTTP_POST, handleRecStart);
    server.on("/rec_stop", HTTP_POST, handleRecStop);
    server.on("/rec_status", HTTP_GET, handleRecStatus);
#if defined(DRIVER_T2CAN_DUAL) && !defined(DASH_SINGLE_CAN_STANDALONE)
    server.on("/service_mode", HTTP_GET, handleServiceMode);
    server.on("/service_mode", HTTP_POST, handleServiceMode);
    server.on("/stalk_test", HTTP_GET, handleStalkTest);
    server.on("/burst", HTTP_GET, handleBurst);
    server.on("/burst", HTTP_POST, handleBurst);
    server.on("/bus2_ids", HTTP_GET, handleBus2Ids);
#endif
    server.on("/rec_download", HTTP_GET, handleRecDownload);
    server.on("/disable", HTTP_POST, handleDisable);
    server.on("/reboot", HTTP_GET, handleReboot);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.on("/update", HTTP_POST, handleOtaResult, handleOtaUpload);
    server.on("/ota_creds", HTTP_GET, handleOtaCreds);
    server.on("/ap_config", HTTP_POST, handleApConfig);
    server.on("/ap_status", HTTP_GET, handleApStatus);
    server.on("/can_pins", HTTP_GET, handleCanPins);
    server.on("/can_pins", HTTP_POST, handleCanPinsSave);
    server.on("/settings_export", HTTP_GET, handleSettingsExport);
    server.on("/settings_import", HTTP_POST, handleSettingsImport);
    server.on("/wifi_scan", HTTP_GET, handleWifiScan);
    server.on("/wifi_config", HTTP_POST, handleWifiConfig);
    server.on("/wifi_status", HTTP_GET, handleWifiStatus);
    server.on("/system_status", HTTP_GET, handleSystemStatus);
    server.on("/task_stats", HTTP_GET, handleTaskStats);
    server.on("/wifi_networks", HTTP_GET, handleWifiNetworks);
    server.on("/wifi_connect", HTTP_POST, handleWifiConnect);
    server.on("/wifi_delete", HTTP_POST, handleWifiDelete);
    server.on("/update_check", HTTP_GET, handleUpdateCheck);
    server.on("/update_install", HTTP_POST, handleUpdateInstall);
    server.on("/update_beta", HTTP_GET, handleUpdateBeta);
    server.on("/update_beta", HTTP_POST, handleUpdateBeta);
    server.on("/auto_update", HTTP_GET, handleAutoUpdate);
    server.on("/auto_update", HTTP_POST, handleAutoUpdate);
#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)
    server.on("/gateway_status", HTTP_GET, handleGatewayStatus);
    server.on("/gateway_dns", HTTP_GET, handleGatewayDnsGet);
    server.on("/gateway_dns", HTTP_POST, handleGatewayDnsPost);
    server.on("/gateway_dns_test", HTTP_GET, handleGatewayDnsTest);
    server.on("/gateway_dns_stats_reset", HTTP_POST, handleGatewayDnsStatsReset);
    server.on("/gateway_whitelist_add", HTTP_POST, handleGatewayWhitelistAdd);
    server.on("/gateway_blocked", HTTP_GET, handleGatewayBlocked);
    server.on("/gateway_blocked_clear", HTTP_POST, handleGatewayBlockedClear);
#endif
#if defined(DASH_PLUGIN_ENGINE)
    server.on("/plugins/status", HTTP_GET, handlePluginsStatus);
    server.on("/plugins/install_url", HTTP_POST, handlePluginsInstallUrl);
    server.on("/plugins/upload", HTTP_POST, handlePluginsUpload, handlePluginsUploadChunks);
    server.on("/plugins/install_json", HTTP_POST, handlePluginsInstallJson);
    server.on("/plugins/toggle", HTTP_POST, handlePluginsToggle);
    server.on("/plugins/priority", HTTP_POST, handlePluginsPriority);
    server.on("/plugins/remove", HTTP_POST, handlePluginsRemove);
    server.on("/plugins/replay_count", HTTP_POST, handlePluginsReplayCount);
    server.on("/plugins/rule_test", HTTP_POST, handlePluginsRuleTest);
#endif

    server.begin();
    if (strlen(staSSID) > 0)
        dashScheduleSTAConnect(kDashStaBootDelayMs);
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(webTask, "web", 8192, nullptr, 1, nullptr);
#else
    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 1);
#endif
    Serial.println("[WEB] Dashboard: http://" + WiFi.softAPIP().toString());
    dashLog("[BOOT] ev-open-can-tools ready");
}

static void mcpDashboardLoop()
{
    if (Update.isRunning())
        return;
    dashSerialDiagnosticsPoll();
    if (recActive && (millis() - recStartMs >= kRecMaxDurationMs))
        dashStopRecordingAndSave("time limit");
    dashCheckBusHealth();
    if (canOnline && millis() - lastFrameMs > 10000)
    {
        canOnline = false;
        dashLog("[CAN] Bus OFFLINE (timeout)");
    }
#if defined(DASH_RGB_STATUS_LED)
    appRefreshStatusLed(false);
#endif
    // Phase 1: WiFi自动关闭检查（flag-based，不直接调WiFi.mode）
    if (dashPowerMgmtShouldDisableWifi())
    {
        WiFi.mode(WIFI_AP);
        dashPowerMgmtMarkWifiDisabled();
        dashLog("[PWR] WiFi STA auto-off (idle timeout)");
    }
    // Phase 1: 自动关机检查（deep sleep）
    dashPowerMgmtTick();
}

#endif
