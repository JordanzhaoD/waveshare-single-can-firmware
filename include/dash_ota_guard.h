#pragma once
// dash_ota_guard.h — 车辆OTA保护（Phase 1）
// 监听0x318帧，检测车辆OTA状态，暂停FSD注入
// 零侵入：仅设置全局标志，不修改Handler内部逻辑

#include "can_frame_types.h"

// ── 全局状态 ──────────────────────────────────────────
static volatile bool vehicleOtaActive = false; // true=车辆OTA进行中，暂停注入

// ── 内部计数器 ────────────────────────────────────────
static volatile uint8_t otaConfirmCount = 0; // 连续检测到OTA的次数
static volatile uint8_t otaClearCount = 0;   // 连续未检测到OTA的次数

// ── 配置 ─────────────────────────────────────────────
#define OTA_CONFIRM_THRESHOLD 3 // 连续3次确认OTA
#define OTA_CLEAR_THRESHOLD 6   // 连续6次确认OTA结束

// ── 核心检测函数 ─────────────────────────────────────
// 在CAN-A帧回调中调用，传入帧
inline void dashOtaGuardProcessFrame(const CanFrame &frame)
{
    if (frame.id != CAN_ID_OTA_STATUS)
        return;
    if (frame.dlc < 7)
        return;

    bool otaFlag = (frame.data[6] & 0x03) == 0x02;

    if (otaFlag)
    {
        otaConfirmCount = otaConfirmCount + 1;
        otaClearCount = 0;
        if (otaConfirmCount >= OTA_CONFIRM_THRESHOLD && !vehicleOtaActive)
        {
            vehicleOtaActive = true;
        }
    }
    else
    {
        otaClearCount = otaClearCount + 1;
        otaConfirmCount = 0;
        if (otaClearCount >= OTA_CLEAR_THRESHOLD && vehicleOtaActive)
        {
            vehicleOtaActive = false;
        }
    }
}

// ── 注入门禁检查 ─────────────────────────────────────
// 返回true=允许注入，false=OTA进行中应暂停
inline bool dashOtaGuardAllowInjection()
{
    return !vehicleOtaActive;
}

// ── 状态查询 ─────────────────────────────────────────
inline bool dashOtaGuardStatus() { return vehicleOtaActive; }
