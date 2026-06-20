# FSD 激活备用插件

这里保存 FSD 激活相关的插件 JSON 文件作为备用。

## 当前固件状态

**自 2026-05-08 起，HW3/HW4 的 FSD 激活逻辑已硬编码到 [handlers.h](../../include/handlers.h)**，
不再依赖插件。这套备用插件**不需要安装**，正常使用没有作用。

## 何时使用这些备用插件

仅在以下场景：

1. 你回退到了不含内置激活的旧固件版本
2. 你修改 `handlers.h` 移除了内置激活并希望恢复
3. 你想自定义激活位（修改 JSON 然后安装覆盖）
4. 升级新版本固件后内置逻辑出问题，临时用插件兜底

## 文件清单

### HW3
- `ad-activation-hw3.json` — 1021 mux 0 写 bit 46 = 1（FSD 激活闩锁）
- `nag-suppression-hw3.json` — 1021 mux 1 写 bit 19 = 0（唠叨抑制）+ bit 46 = 1
- `bypass-tlssc-hw3.json` — 1021 mux 0 写 bit 38 = 1（TLSSC 旁路）

### HW4
- `ad-activation-hw4.json` — 1021 mux 0 写 bit 46 = 1（激活）+ bit 60 = 1（HW4 enable）
- `nag-suppression-hw4.json` — 1021 mux 1 写 bit 19 = 0（唠叨）+ bit 47 = 1（**HW4 必需的 FSD ready**）
- `bypass-tlssc-hw4.json` — 1021 mux 0 写 bit 38 = 1（TLSSC 旁路）
- `emergency-vehicle-detection-hw4.json` — 1021 mux 0 写 bit 59 = 1（紧急车辆识别）
- `isa-chime-suppress-hw4.json` — 921 写 byte 1 bit 5 = 1（ISA 提示音抑制）

## 安装方法

1. 仪表盘 → 插件卡片
2. "粘贴 JSON" 把上面文件内容贴进去 → "从 JSON 安装"
3. 或者点 "上传 .json" 直接选文件
4. 安装后确认插件列表里 toggle 是绿色（已启用）
5. 顶部 "CAN 注入" 必须开启
6. 关闭 "AP 注入门控" 或等车 AP 真激活

## 与原版插件的区别

这些是 `docs/examples/HW3/` 和 `docs/examples/HW4/` 的拷贝，目录另起是为了
"FSD 激活功能"集中可见，避免和 speed-offset、ISA-chime 等其他用途插件混在一起。
