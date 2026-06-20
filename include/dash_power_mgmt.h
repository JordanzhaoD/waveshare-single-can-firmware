#pragma once
// dash_power_mgmt.h — 功耗管理（Phase 1）
// 自动关机：5分钟无CAN数据 → deep sleep，DASH_WAKE_PIN/TWAI_RX_PIN 唤醒
// WiFi自动关闭：5分钟无Web请求 → 仅关STA保AP

#include <cstdint>
#include <esp_sleep.h>

// ── 配置参数 ─────────────────────────────────────────
#define CAN_TIMEOUT_MS       300000   // 5分钟
#define WEB_TIMEOUT_MS       300000   // 5分钟
#define NVS_KEY_AUTO_SHUTDOWN "auto_shutdown"
#define NVS_KEY_WIFI_AUTO_OFF "wifi_auto_off"

#ifndef DASH_WAKE_PIN
#ifdef TWAI_RX_PIN
#define DASH_WAKE_PIN TWAI_RX_PIN
#else
#define DASH_WAKE_PIN GPIO_NUM_4
#endif
#endif

// ── 全局状态 ─────────────────────────────────────────
static volatile bool autoShutdownEnabled = false;
static volatile bool wifiAutoOffEnabled  = false;
static volatile uint32_t lastCanActivityMs  = 0;
static volatile uint32_t lastWebActivityMs  = 0;
static volatile bool wifiStaDisabled = false;       // STA已关闭标记

// Forward declarations for Arduino/ESP-IDF functions used
// These are available in the ESP-IDF/Arduino environment
extern unsigned long millis();

// WiFi mode constants - use WiFi object available globally
// WiFi.mode(WIFI_AP) = AP only, WiFi.mode(WIFI_AP_STA) = AP+STA

// ── 初始化 ───────────────────────────────────────────
inline void dashPowerMgmtConfigureWake() {
    esp_sleep_enable_ext0_wakeup(DASH_WAKE_PIN, 1);
}

inline void dashPowerMgmtInit() {
    if (autoShutdownEnabled) {
        dashPowerMgmtConfigureWake();
    }
    lastCanActivityMs = millis();
    lastWebActivityMs = millis();
}

// ── CAN活动更新 ──────────────────────────────────────
inline void dashPowerMgmtTouchCan() {
    lastCanActivityMs = millis();
}

// ── Web活动更新 ──────────────────────────────────────
// Also signals that STA should be restored if it was disabled
inline void dashPowerMgmtTouchWeb() {
    lastWebActivityMs = millis();
    wifiStaDisabled = false;  // Signal to restore STA in next loop
}

// ── 主循环检查 ───────────────────────────────────────
// Returns true if device is about to enter deep sleep
inline bool dashPowerMgmtTick() {
    uint32_t now = millis();

    // Auto-shutdown: deep sleep after CAN timeout
    if (autoShutdownEnabled && (now - lastCanActivityMs) > CAN_TIMEOUT_MS) {
        dashPowerMgmtConfigureWake();
        esp_deep_sleep_start();
        return true;
    }

    return false;
}

// ── Check if WiFi STA should be disabled (called from main loop) ──
inline bool dashPowerMgmtShouldDisableWifi() {
    if (!wifiAutoOffEnabled || wifiStaDisabled) return false;
    return (millis() - lastWebActivityMs) > WEB_TIMEOUT_MS;
}

inline void dashPowerMgmtMarkWifiDisabled() {
    wifiStaDisabled = true;
}
