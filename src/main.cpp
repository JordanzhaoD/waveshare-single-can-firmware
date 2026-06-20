/*
    PlatformIO entry point.
    Shared build settings live in platformio_profile.h.
    Logic is in the shared headers under include/.
*/

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <Arduino.h>
#endif
#ifdef DRIVER_T2CAN_DUAL
// LILYGO T-2Can: native TWAI is the primary bus (FSD/speed pipeline, unchanged);
// an MCP2515 over SPI is a second bus monitored alongside it. Drive the primary
// through the existing DRIVER_TWAI path and bolt the secondary on top. DRIVER_TWAI
// must be defined before app.h so its CAN-task globals are declared.
#define DRIVER_TWAI
#ifndef T2CAN_SECONDARY_BUS
#define T2CAN_SECONDARY_BUS CAN_BUS_PARTY
#endif
#endif

#include "app.h"
#include "dash_tx_evidence.h"

#ifdef DRIVER_T2CAN_DUAL
#include "drivers/esp32_mcp2515_driver.h"
#endif

#ifdef DRIVER_MCP2515
#include <SPI.h>
#include "drivers/mcp2515_driver.h"
#elif defined(DRIVER_ESP32_EXT_MCP2515)
#ifndef ESP_PLATFORM
#include <SPI.h>
#endif
#include "drivers/esp32_mcp2515_driver.h"
#elif defined(DRIVER_SAME51)
#include "drivers/same51_driver.h"
#elif defined(DRIVER_TWAI)
#include "drivers/twai_driver.h"
#ifndef ESP_PLATFORM
#include <Preferences.h>
#endif
#ifndef TWAI_TX_PIN
#define TWAI_TX_PIN GPIO_NUM_5
#endif
#ifndef TWAI_RX_PIN
#define TWAI_RX_PIN GPIO_NUM_4
#endif
#else
#error "Define DRIVER_MCP2515, DRIVER_ESP32_EXT_MCP2515, DRIVER_SAME51, or DRIVER_TWAI in build_flags"
#endif

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI)
static bool appTwaiGpioReserved(gpio_num_t pin)
{
    int p = static_cast<int>(pin);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (p >= 26 && p <= 32)
        return true; // embedded flash/PSRAM bus on ESP32-S3 modules
    if (p == 45 || p == 46)
        return true; // strapping/input-only pins
#elif defined(CONFIG_IDF_TARGET_ESP32)
#if !defined(DASH_ALLOW_CAN_GPIO_6_11) || !DASH_ALLOW_CAN_GPIO_6_11
    if (p >= 6 && p <= 11)
        return true; // SPI flash pins on common ESP32 modules
#endif
#endif
    return false;
}

static bool appTwaiGpioValid(gpio_num_t pin, bool tx)
{
    int p = static_cast<int>(pin);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int kMaxGpio = 48;
#else
    constexpr int kMaxGpio = 39;
#endif
    if (p < 0 || p > kMaxGpio || appTwaiGpioReserved(pin))
        return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (tx && p >= 34 && p <= 39)
        return false; // input-only pins cannot drive TWAI TX
#else
    (void)tx;
#endif
    return true;
}
#endif

#ifdef DRIVER_T2CAN_DUAL
static std::unique_ptr<ESP32_MCP2515Driver> appDriverSecondary;
static volatile uint32_t t2canSecondaryRxCount = 0;
static volatile uint32_t t2canSecondaryTxCount = 0;
static volatile uint32_t t2canSecondaryTxErrCount = 0;
static volatile uint8_t t2canSecondaryEflg = 0;
static DashTxEvidence serviceTxEvidence;
static DashTxEvidence fogTxEvidence;
static DashTxEvidence stalkTxEvidence;
const DashTxEvidence &t2canFogTxEvidence(void) { return fogTxEvidence; }
const DashTxEvidence &t2canStalkTxEvidence(void) { return stalkTxEvidence; }
static volatile uint8_t g_svcLastCommand = 0; // 0=none, 1=enter, 2=exit

static bool t2canTxSecondaryCounted(const CanFrame &f)
{
    if (!appDriverSecondary)
        return false;
    bool ok = appDriverSecondary->send(f);
    t2canSecondaryTxCount = t2canSecondaryTxCount + 1;
    if (!ok)
        t2canSecondaryTxErrCount = t2canSecondaryTxErrCount + 1;
    return ok;
}

static void t2canSetupSecondary()
{
#ifdef MCP2515_RST_PIN
    gpio_set_direction((gpio_num_t)MCP2515_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)MCP2515_RST_PIN, 1);
    delay(20);
    gpio_set_level((gpio_num_t)MCP2515_RST_PIN, 0);
    delay(20);
    gpio_set_level((gpio_num_t)MCP2515_RST_PIN, 1);
    delay(20);
#endif
    appDriverSecondary = std::make_unique<ESP32_MCP2515Driver>(PIN_CAN_CS);
    if (!appDriverSecondary->init())
    {
        Serial.println("CAN B (MCP2515) init failed");
        return;
    }
    appDriverSecondary->mcp().setReceiveAllMode();
    Serial.println("CAN B (MCP2515) ready @ 500k");
}

// Transmit on the secondary bus. No automatic logic targets bus B yet
// (service-mode / lighting features land later); this is the TX entry point.
bool t2canSendSecondary(const CanFrame &frame)
{
    if (!appDriverSecondary)
        return false;
    CanFrame f = frame;
    f.bus = T2CAN_SECONDARY_BUS;
    return t2canTxSecondaryCounted(f);
}

// ── bus2 discovered-ID table (X197 9/10) — for serial log + dashboard /bus2_ids ──
struct T2canBus2Id
{
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t count;
};
static constexpr uint16_t kT2canBus2MaxIds = 160;
static T2canBus2Id g_bus2Ids[kT2canBus2MaxIds];
static volatile uint16_t g_bus2IdCount = 0;

// Latest rolling counter seen on the real 0x249 SCCMLeftStalk stream, so an
// injected test frame can ride just after it with the next counter value.
static volatile uint8_t g_stalkLastCounter = 0;
static void t2canBurstCheckTrigger(uint8_t status, uint32_t now);

static void t2canWheelDndRecordNative(const CanFrame &f)
{
    dashWheelDndCtrl.recordNative3c2(f.data, millis());
}

static void t2canRecordBus2(const CanFrame &f)
{
    if (f.id & 0x80000000UL)
        return; // standard 11-bit IDs only (Tesla lighting/stalk are standard)
    uint16_t sid = (uint16_t)(f.id & 0x7FF);
    if (sid == 0x249 && f.dlc >= 2)
    {
        g_stalkLastCounter = f.data[1] & 0x0F; // track real stalk counter
        uint8_t status = (f.data[1] >> 4) & 0x07;
        t2canBurstCheckTrigger(status, millis());
    }
    if (sid == 0x3C2 && f.dlc >= 8)
        t2canWheelDndRecordNative(f);
    uint16_t n = g_bus2IdCount;
    for (uint16_t i = 0; i < n; i++)
    {
        if (g_bus2Ids[i].id == sid)
        {
            g_bus2Ids[i].dlc = f.dlc;
            memcpy(g_bus2Ids[i].data, f.data, 8);
            g_bus2Ids[i].count = g_bus2Ids[i].count + 1;
            return;
        }
    }
    if (n < kT2canBus2MaxIds)
    {
        g_bus2Ids[n].id = sid;
        g_bus2Ids[n].dlc = f.dlc;
        memcpy(g_bus2Ids[n].data, f.data, 8);
        g_bus2Ids[n].count = 1;
        g_bus2IdCount = n + 1; // publish count last so readers never see uninit slots
        Serial.printf("bus2 new id 0x%03X dlc=%u\n", sid, f.dlc);
    }
}

// Accessors used by the dashboard /bus2_ids handler.
uint16_t t2canBus2IdCount(void) { return g_bus2IdCount; }
uint32_t t2canBus2RxCount(void) { return t2canSecondaryRxCount; }
uint32_t t2canBus2TxCount(void) { return t2canSecondaryTxCount; }
uint32_t t2canBus2TxErrCount(void) { return t2canSecondaryTxErrCount; }
uint8_t t2canBus2Eflg(void) { return t2canSecondaryEflg; }
const DashTxEvidence &t2canServiceTxEvidence(void) { return serviceTxEvidence; }
uint8_t t2canServiceLastCommand(void) { return g_svcLastCommand; }
bool t2canBus2IdAt(uint16_t i, uint16_t *id, uint8_t *dlc, uint8_t data[8], uint32_t *count)
{
    if (i >= g_bus2IdCount)
        return false;
    *id = g_bus2Ids[i].id;
    *dlc = g_bus2Ids[i].dlc;
    memcpy(data, g_bus2Ids[i].data, 8);
    *count = g_bus2Ids[i].count;
    return true;
}

static void t2canDrainSecondary()
{
    if (!appDriverSecondary)
        return;
    CanFrame f;
    for (uint8_t budget = 16; budget; budget--)
    {
        if (!appDriverSecondary->read(f))
            break;
        f.bus = T2CAN_SECONDARY_BUS;
        t2canSecondaryRxCount = t2canSecondaryRxCount + 1;
        t2canRecordBus2(f);
#ifdef ESP32_DASHBOARD
        dashRecordCanFrame(f, 'R');
        // CAN B is Party CAN — the FSD activation frame (0x3EE/0x3FD) lives here.
        // Dispatch FSD-relevant frames to the active handler so injection can
        // modify and retransmit them on the same bus (MCP2515).
        // NOTE: Do NOT call dashPostProcessFrame here — the handler already
        // injects, and calling it again would double-send (DASH_FSD_252_COMPAT=1),
        // overflowing the MCP2515's 3-slot TX buffer and causing Tx errors.
        if (appActiveHandler)
        {
            appActiveHandler->handleMessage(f, *appDriverSecondary);
        }
#endif
    }
}

// ── Service mode (BODY bus / bus B): VCSEC_serviceDiagnosticRequest 0x339 ──
// Spec 2.4.1: send 4 frames at 10ms spacing on the BODY bus. The signal lives at
// start bit 47 (Intel) = byte5 bit7: 1 = enter service mode, 0 = exit.
//   activate   -> 00 00 00 00 00 80 00 00
//   deactivate -> 00 00 00 00 00 00 00 00
// RAM-only flag, OFF on boot; each dashboard /service_mode toggle fires one burst.
static volatile bool g_t2canServiceMode = false;
static volatile uint8_t g_svcBurstRemaining = 0;
static volatile uint8_t g_svcBurstValue = 0;

void t2canSetServiceMode(bool on)
{
    g_t2canServiceMode = on;
    g_svcLastCommand = on ? 1 : 2;
    g_svcBurstValue = on ? 0x80 : 0x00;
    g_svcBurstRemaining = 4;
}
bool t2canGetServiceMode(void) { return g_t2canServiceMode; }
uint8_t t2canServiceBurstRemaining(void) { return g_svcBurstRemaining; }

static bool recordServiceTxEvidence(const CanFrame &f, uint32_t now)
{
    serviceTxEvidence.recordAttempt(f.id, f.bus, f.dlc, f.data, now);
    bool ok = t2canTxSecondaryCounted(f);
    serviceTxEvidence.recordResult(ok);
    return ok;
}

static void t2canServiceModeTick()
{
    if (g_svcBurstRemaining == 0 || !appDriverSecondary)
        return;
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 10)
        return;
    last = now;
    CanFrame f = {};
    f.id = 0x339;
    f.dlc = 8;
    f.data[5] = g_svcBurstValue;
    f.bus = T2CAN_SECONDARY_BUS;
    bool ok = recordServiceTxEvidence(f, now);
#ifdef ESP32_DASHBOARD
    dashRecordCanFrame(f, ok ? 'T' : 'E');
#endif
    g_svcBurstRemaining = g_svcBurstRemaining - 1;
}

// ── Stalk injection test (0x249 SCCMLeftStalk on bus B / X197 9/10) ──
// CRC reverse-engineered from a 2023.11 Model Y HW3 capture (846/846 verified):
//   b0 = BASE[counter] XOR OFFSET[status]   (valid while b2=b3=0)
// status: 1=PULL (flash/超车闪), 2=PUSH (high-beam toggle/远光). See
// docs/stalk_0x249_crc_solved_zh.md. Tests whether plain injection (no MITM)
// can drive the lights despite the genuine SCCM also transmitting 0x249.
static const uint8_t kStalkBase[16] = {
    0x9B, 0xE8, 0x2A, 0xD3, 0xD3, 0x83, 0x4C, 0x5E,
    0x3F, 0x5E, 0xE2, 0x28, 0x3A, 0x13, 0xAF, 0xCE};
static inline uint8_t t2canStalkCrc(uint8_t counter, uint8_t status)
{
    static const uint8_t off[8] = {0x00, 0x76, 0xEC, 0x00, 0xF7, 0x00, 0x00, 0x00};
    return kStalkBase[counter & 0x0F] ^ off[status & 0x07];
}

static volatile uint8_t g_stalkInjStatus = 0;  // 0=off, 1=PULL, 2=PUSH
static volatile uint32_t g_stalkInjUntil = 0;  // millis() deadline

// ── Flash Burst (real double-PULL trigger) ───────────────────────
// RAM-only and OFF on boot. When enabled, two real 0x249 PULL edges within
// 2 seconds arm a firmware-timed PULL burst that reuses t2canStalkInjectTick().
static volatile bool g_burstEnabled = false;
static volatile uint8_t g_burstCount = 3;
static volatile uint16_t g_burstOnMs = 180;
static volatile uint16_t g_burstOffMs = 180;
static volatile uint32_t g_burstLastPullMs = 0;
static volatile uint32_t g_burstLastTriggerMs = 0;
static volatile uint8_t g_burstPrevStatus = 0;
static volatile uint8_t g_burstPhasesLeft = 0;
static volatile bool g_burstOnPhase = false;
static volatile bool g_burstPhaseActive = false;
static volatile uint32_t g_burstPhaseEnd = 0;
static volatile bool g_burstOwnsStalk = false;

static constexpr uint32_t kBurstDoublePullWindowMs = 2000;
static constexpr uint8_t kBurstMinCount = 1;
static constexpr uint8_t kBurstMaxCount = 20;
static constexpr uint16_t kBurstMinPhaseMs = 80;
static constexpr uint16_t kBurstMaxPhaseMs = 1000;

static void t2canBurstStop(bool clearOwnedStalk)
{
    g_burstPhasesLeft = 0;
    g_burstOnPhase = false;
    g_burstPhaseActive = false;
    g_burstPhaseEnd = 0;
    g_burstLastPullMs = 0;
    if (clearOwnedStalk && g_burstOwnsStalk && g_stalkInjStatus == 1)
    {
        g_stalkInjStatus = 0;
        g_stalkInjUntil = 0;
    }
    g_burstOwnsStalk = false;
}

void t2canSetBurstEnabled(bool on)
{
    g_burstEnabled = on;
    if (!on)
        t2canBurstStop(true);
}

bool t2canGetBurstEnabled(void) { return g_burstEnabled; }

void t2canSetBurstParams(uint8_t count, uint16_t onMs, uint16_t offMs)
{
    if (count < kBurstMinCount)
        count = kBurstMinCount;
    if (count > kBurstMaxCount)
        count = kBurstMaxCount;
    if (onMs < kBurstMinPhaseMs)
        onMs = kBurstMinPhaseMs;
    if (onMs > kBurstMaxPhaseMs)
        onMs = kBurstMaxPhaseMs;
    if (offMs < kBurstMinPhaseMs)
        offMs = kBurstMinPhaseMs;
    if (offMs > kBurstMaxPhaseMs)
        offMs = kBurstMaxPhaseMs;
    g_burstCount = count;
    g_burstOnMs = onMs;
    g_burstOffMs = offMs;
}

uint8_t t2canGetBurstCount(void) { return g_burstCount; }
uint16_t t2canGetBurstOnMs(void) { return g_burstOnMs; }
uint16_t t2canGetBurstOffMs(void) { return g_burstOffMs; }
bool t2canBurstIsRunning(void) { return g_burstPhaseActive || g_burstPhasesLeft > 0; }
uint8_t t2canBurstPhasesLeft(void) { return g_burstPhasesLeft; }
uint32_t t2canBurstLastPullMs(void) { return g_burstLastPullMs; }
uint32_t t2canBurstLastTriggerMs(void) { return g_burstLastTriggerMs; }
uint8_t t2canBurstPrevStatus(void) { return g_burstPrevStatus; }

static void t2canBurstStart(uint32_t now)
{
    t2canBurstStop(true);
    g_burstLastTriggerMs = now;
    g_burstPhasesLeft = static_cast<uint8_t>(g_burstCount * 2);
    g_burstOnPhase = false;
    g_burstPhaseActive = false;
    g_burstPhaseEnd = now;
}

static void t2canBurstCheckTrigger(uint8_t status, uint32_t now)
{
    if (!g_burstEnabled)
    {
        g_burstPrevStatus = status;
        g_burstLastPullMs = 0;
        return;
    }
    bool pullEdge = g_burstPrevStatus != 1 && status == 1;
    g_burstPrevStatus = status;
    if (!pullEdge || t2canBurstIsRunning())
        return;
    if (g_burstLastPullMs != 0 && (uint32_t)(now - g_burstLastPullMs) <= kBurstDoublePullWindowMs)
    {
        t2canBurstStart(now);
        g_burstLastPullMs = 0;
        return;
    }
    g_burstLastPullMs = now;
}

static void t2canBurstTick()
{
    if (!g_burstEnabled || (!g_burstPhaseActive && g_burstPhasesLeft == 0))
    {
        if (!g_burstEnabled)
            t2canBurstStop(true);
        return;
    }
    uint32_t now = millis();
    if (g_burstPhaseActive && (int32_t)(now - g_burstPhaseEnd) < 0)
    {
        if (g_burstOnPhase)
        {
            g_stalkInjStatus = 1;
            g_stalkInjUntil = g_burstPhaseEnd + 30;
            g_burstOwnsStalk = true;
        }
        return;
    }
    if (g_burstPhaseActive)
    {
        if (g_burstOnPhase && g_burstOwnsStalk && g_stalkInjStatus == 1)
        {
            g_stalkInjStatus = 0;
            g_stalkInjUntil = 0;
            g_burstOwnsStalk = false;
        }
        g_burstPhaseActive = false;
        if (g_burstPhasesLeft == 0)
        {
            g_burstOnPhase = false;
            return;
        }
    }
    g_burstOnPhase = !g_burstOnPhase;
    g_burstPhaseActive = true;
    g_burstPhasesLeft = g_burstPhasesLeft - 1;
    if (g_burstOnPhase)
    {
        g_stalkInjStatus = 1;
        g_burstPhaseEnd = now + g_burstOnMs;
        g_stalkInjUntil = g_burstPhaseEnd + 30;
        g_burstOwnsStalk = true;
    }
    else
    {
        if (g_burstOwnsStalk && g_stalkInjStatus == 1)
        {
            g_stalkInjStatus = 0;
            g_stalkInjUntil = 0;
        }
        g_burstOwnsStalk = false;
        g_burstPhaseEnd = now + g_burstOffMs;
    }
}

void t2canStalkTest(uint8_t status, uint16_t durationMs)
{
    t2canBurstStop(true);
    g_stalkInjStatus = status;
    g_stalkInjUntil = millis() + durationMs;
}

static void t2canBus2HealthTick()
{
    if (!appDriverSecondary)
        return;
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 500)
        return;
    last = now;
    t2canSecondaryEflg = appDriverSecondary->mcp().getErrorFlags();
}

static void t2canStalkInjectTick()
{
    if (!appDriverSecondary || g_stalkInjStatus == 0)
        return;
    uint32_t now = millis();
    if ((int32_t)(now - g_stalkInjUntil) >= 0)
    {
        g_stalkInjStatus = 0;
        return;
    }
    static uint32_t last = 0;
    if (now - last < 50) // match real 0x249 cadence (~20Hz)
        return;
    last = now;
    uint8_t st = g_stalkInjStatus;
    uint8_t cnt = (g_stalkLastCounter + 1) & 0x0F; // ride after newest real frame
    CanFrame f = {};
    f.id = 0x249;
    f.dlc = 4;
    f.data[0] = t2canStalkCrc(cnt, st);
    f.data[1] = (uint8_t)((st << 4) | cnt);
    f.data[2] = 0;
    f.data[3] = 0;
    f.bus = T2CAN_SECONDARY_BUS;
    stalkTxEvidence.recordAttempt(f.id, f.bus, f.dlc, f.data, now);
    bool ok = t2canTxSecondaryCounted(f);
    stalkTxEvidence.recordResult(ok);
#ifdef ESP32_DASHBOARD
    dashRecordCanFrame(f, ok ? 'T' : 'E');
#endif
}

// ── Fog light tick (Phase 4: 0x273 on bus B) ──────────────────
static uint32_t g_fogLastMs = 0;
static constexpr uint32_t kT2canGearFreshMs = 500;

static void t2canSendFogFrame(const uint8_t data[8])
{
    CanFrame f = {};
    f.id = CAN_ID_REAR_FOG_LIGHT;
    f.dlc = 8;
    memcpy(f.data, data, 8);
    f.bus = T2CAN_SECONDARY_BUS;
    uint32_t now = millis();
    fogTxEvidence.recordAttempt(f.id, f.bus, f.dlc, f.data, now);
    bool ok = t2canTxSecondaryCounted(f);
    fogTxEvidence.recordResult(ok);
#ifdef ESP32_DASHBOARD
    dashRecordCanFrame(f, ok ? 'T' : 'E');
#endif
}

static bool t2canGearIsFreshDrive(uint32_t now)
{
    return apRestoreState.gearSeen && apRestoreState.gearRaw == 4 &&
           (now - apRestoreState.gearMs) <= kT2canGearFreshMs;
}

static void t2canFogLightTick()
{
    if (!appDriverSecondary)
        return;
    uint32_t now = millis();
    bool active = dashFogCtrl.isActive();
    bool safeGear = t2canGearIsFreshDrive(now);

    if (dashFogOffRequested || (active && !safeGear))
    {
        uint8_t offData[8];
        dashFogCtrl.buildFrame(offData, false);
        t2canSendFogFrame(offData);
        dashFogCtrl.stop();
        dashFogOffRequested = false;
        g_fogLastMs = 0;
        return;
    }

    if (!active)
        return;

    uint32_t elapsed = g_fogLastMs ? (now - g_fogLastMs) : 0;
    g_fogLastMs = now;
    uint8_t data[8];
    if (dashFogCtrl.tick((int)elapsed, apRestoreState.gearRaw, data))
        t2canSendFogFrame(data);
    if (!dashFogCtrl.isActive())
        g_fogLastMs = 0;
}

// ── Wheel DND tick (Phase 3: 0x3C2 on bus B) ──────────────────
static bool g_wheelDndGateWasOpen = false;

static void t2canWheelDndTick()
{
    if (!appDriverSecondary)
        return;
    bool gateOpen = canActive && dashDefenseEnabled && (dashDndVolume || dashDndSpeed);
    if (!gateOpen)
    {
        dashWheelDndCtrl.reset();
        g_wheelDndGateWasOpen = false;
        return;
    }
    if (!g_wheelDndGateWasOpen)
    {
        if (dashDndVolume)
            dashWheelDndCtrl.startVolume();
        if (dashDndSpeed)
            dashWheelDndCtrl.startSpeed();
        g_wheelDndGateWasOpen = true;
    }
    if (!dashWheelDndCtrl.isRunning())
        return;
    uint8_t data[8];
    if (dashWheelDndCtrl.tick((int)millis(), data))
    {
        CanFrame f = {};
        f.id = 0x3C2;
        f.dlc = 8;
        memcpy(f.data, data, 8);
        f.bus = T2CAN_SECONDARY_BUS;
        dashWheelDndCtrl.diag.tx.recordAttempt(f.id, f.bus, f.dlc, f.data, millis());
        bool ok = t2canTxSecondaryCounted(f);
        dashWheelDndCtrl.diag.tx.recordResult(ok);
        dashWheelDndCtrl.diag.sequenceState = ok ? "sent" : "failed";
#ifdef ESP32_DASHBOARD
        dashRecordCanFrame(f, ok ? 'T' : 'E');
#endif
    }
}
#endif

static void app_main_setup()
{
#ifdef DRIVER_MCP2515
    appSetup<MCP2515Driver>(std::make_unique<MCP2515Driver>(PIN_CAN_CS), "MCP25625 ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get());
#endif
#elif defined(DRIVER_ESP32_EXT_MCP2515)
#ifndef ESP_PLATFORM
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, PIN_CAN_CS);
    SPI.setFrequency(8000000);
#endif
    auto drv = std::make_unique<ESP32_MCP2515Driver>(PIN_CAN_CS);
    MCP2515 *mcpPtr = &drv->mcp();
    appSetup<ESP32_MCP2515Driver>(std::move(drv), "ESP32 + MCP2515 ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get(), mcpPtr);
#endif
#elif defined(DRIVER_SAME51)
    appSetup<SAME51Driver>(std::make_unique<SAME51Driver>(), "SAME51 CAN ready @ 500k");
#elif defined(DRIVER_TWAI)
    // Load TWAI pins from NVS (survives OTA); fall back to compile-time defaults
    gpio_num_t twaiTx = TWAI_TX_PIN;
    gpio_num_t twaiRx = TWAI_RX_PIN;
    {
        Preferences canPrefs;
        if (canPrefs.begin("can", false))
        {
            int8_t tx = canPrefs.getChar("tx", -1);
            int8_t rx = canPrefs.getChar("rx", -1);
            canPrefs.end();
            if (appTwaiGpioValid((gpio_num_t)tx, true) && appTwaiGpioValid((gpio_num_t)rx, false) && tx != rx)
            {
                twaiTx = (gpio_num_t)tx;
                twaiRx = (gpio_num_t)rx;
            }
        }
    }
    appSetup<TWAIDriver>(std::make_unique<TWAIDriver>(twaiTx, twaiRx), "ESP32 TWAI ready @ 500k");
#ifdef ESP32_DASHBOARD
    mcpDashboardSetup(appHandler.get(), appDriver.get());
#endif
#endif
#ifdef DRIVER_T2CAN_DUAL
    t2canSetupSecondary();
    // Wire the MCP2515 TX callback so the dashboard counts injected frames
    // sent on the secondary (Party CAN) bus.
    if (appDriverSecondary && dashDriver)
        appDriverSecondary->onSendFrame = dashDriver->onSendFrame;
#endif
}

static bool app_main_loop()
{
#ifdef DRIVER_MCP2515
    bool processed = appLoop<MCP2515Driver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#elif defined(DRIVER_ESP32_EXT_MCP2515)
    bool processed = appLoop<ESP32_MCP2515Driver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#elif defined(DRIVER_SAME51)
    return appLoop<SAME51Driver>();
#elif defined(DRIVER_TWAI)
    bool processed = appLoop<TWAIDriver>();
#ifdef ESP32_DASHBOARD
    mcpDashboardLoop();
#endif
    return processed;
#endif
}

#if defined(ESP_PLATFORM) && defined(DRIVER_TWAI)
#ifndef APP_CAN_TASK_STACK
#define APP_CAN_TASK_STACK 6144
#endif
#ifndef APP_CAN_TASK_PRIORITY
#define APP_CAN_TASK_PRIORITY 18
#endif
#ifndef APP_CAN_TASK_CORE
#define APP_CAN_TASK_CORE 0
#endif

static void app_can_task(void *)
{
    appCanTaskDedicated = true;
    for (;;)
    {
        bool processed = appLoop<TWAIDriver>();
#ifdef DRIVER_T2CAN_DUAL
        t2canDrainSecondary();
        t2canServiceModeTick();
        t2canBus2HealthTick();
        t2canBurstTick();
        t2canStalkInjectTick();
        t2canFogLightTick();
        t2canWheelDndTick();
#endif
        appCanTaskLoops = appCanTaskLoops + 1;
        if (!processed)
        {
            appCanTaskIdleLoops = appCanTaskIdleLoops + 1;
            vTaskDelay(1);
        }
    }
}

static bool app_start_can_task()
{
    TaskHandle_t task = nullptr;
#if CONFIG_FREERTOS_UNICORE
    BaseType_t ok = xTaskCreate(app_can_task, "can_rt", APP_CAN_TASK_STACK, nullptr,
                                APP_CAN_TASK_PRIORITY, &task);
#else
    BaseType_t ok = xTaskCreatePinnedToCore(app_can_task, "can_rt", APP_CAN_TASK_STACK, nullptr,
                                            APP_CAN_TASK_PRIORITY, &task, APP_CAN_TASK_CORE);
#endif
    appCanTaskDedicated = ok == pdPASS;
    return ok == pdPASS;
}
#endif

#ifdef ESP_PLATFORM
extern "C" void app_main(void)
{
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvsErr = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvsErr);

    app_main_setup();
#if defined(DRIVER_TWAI)
    bool canTaskStarted = app_start_can_task();
    while (true)
    {
        if (!canTaskStarted)
        {
            if (!app_main_loop())
                vTaskDelay(1);
            continue;
        }
#ifdef ESP32_DASHBOARD
        mcpDashboardLoop();
#endif
        vTaskDelay(1);
    }
#else
    while (true)
    {
        if (!app_main_loop())
            vTaskDelay(1);
    }
#endif
}
#else
void setup()
{
    app_main_setup();
}

void loop()
{
    app_main_loop();
}
#endif
