# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),

## [Unreleased]

### Fixed
- Completed the `firmware20260718.bin` Legacy smart-speed request on `0x3EE` mux 0. Automatic offset now updates byte 3 (`offset + 30`), sets byte 5 bits 0-1, and writes the effective offset percentage to byte 7 bits 0-5, matching the reference firmware's three-field wire protocol.
- Aligned the preferred `0x2F8` map-limit freshness window and downward smoothing behavior with the reference firmware, and added byte-level diagnostics for all modified `0x3EE` fields.

## [1.11] - 2026-07-21

### Added
- **上游 `v3.0.2-beta.8` Built-in NAG 三模式对齐**：新增 Mode A 固定 `+1.80 Nm`、Mode B `+1.80/+1.50/-1.50/-1.80 Nm` 的 1 秒 burst / 1.5 秒 pause，以及使用新鲜 `0x399`、`0x129` 上下文的 Mode C。`0x370` 回显统一执行 counter+1、checksum 重算和 `-1.80..+1.80 Nm` 硬限制。
- NAG 改为独立 dashboard handler，不再依赖当前 Legacy/HW3/HW4 handler；`/status.builtInNag` 增加上下文、候选帧、TX 成败、扭矩、counter 与明确阻断原因。
- Legacy 自动速度偏移新增 `configuredMode`、`sharedStrategy`、`activeHandlerLegacy`、`offsetOnlyTxOk/Fail` 和 `blockedReason` 实车诊断。

### Fixed
- 修复 Waveshare 默认 Auto/HW4 时 NAG 模式写入 Legacy handler、导致 NAG 从未真正执行的问题；选择 NAG 模式现在独立于防护总开关和旧 bionic 开关，同时仍服从 CAN Write、OTA、AP Gate 与 Abort Guard。
- 修复主速度页“自动偏移”只更新 HW3/HW4 `offsetMode`、未启用 Legacy Auto 的问题；共享 Fixed/Auto/Custom 策略现在会同步到 Legacy 并持久化来源状态。
- 修复 Legacy 兼容路径只使用 `0x399` fused limit、忽略已收到 `0x2F8 data[6] & 0x1F` 地图限速的问题。`0x2F8` 两秒内优先，失效后回退 `0x399 data[1] & 0x1F`。
- 将 `0x3EE mux0 byte3[6:1]` 速度偏移与 FSD UI 选择、activation bit46 和 AP-First 稳定计时解耦；偏移可独立发送，但不会提前置 activation bit46。

### Changed
- 自动偏移继续使用参考固件的分段比例、绝对速度上限、上升立即生效和 `5 km/h/s` 下行平滑；最终偏移钳制到 `0..33 km/h`，按 `offset + 30` 编码并保留 byte3 bit0/bit7。
- WebUI 明确主速度策略覆盖 HW3/HW4 与 Legacy；Legacy 专用模式仍可独立选择，并在 NVS/备份恢复中保存策略所有权。

### Safety
- NAG 和 Legacy 自动偏移仍默认关闭；新写入不会绕过 CAN Write、车辆 OTA、父 AP Gate、Abort Guard 或插件所有权冲突检查。
- 本版本已通过 300 项 Python 契约测试（3 skipped）、18 个 native 环境共 633/633、Waveshare ESP32-S3 release build、clang-format、16MB 分区/合并镜像和 SHA-256 校验。实车自动偏移修复仍需发布后道路闭环确认。

## [1.10] - 2026-07-18

### Added
- **Legacy `0x2F8` → `0x3EE` 智能速度偏移**：依据 `firmware20260718.bin`（`tesla-fsd-controller v1.4.35-legacy3ee`）还原真实链路。`0x2F8 / 760` 仅作为 `UI_mppSpeedLimit` 只读首选限速源；有效值超时或无效时回退到 `0x399 / 921` fused limit；最终 `offset + 30` 写入 `0x3EE mux0 byte3[6:1]`，并保留 byte3 的 bit0/bit7。
- `/status.legacySpeed` 新增限速来源、输出百分比、`0x3EE` mux0 byte3 前后值和 `legacy_reference_0x3ee_v1` 实现标识；速度策略页同步展示实际输出帧与限速来源。
- Legacy native 回归覆盖 `0x2F8` 只读、`0x3EE` 手动/自动/自定义/共享三模式输出、fused fallback、位保留、线值钳位和 TX 失败诊断。

### Changed
- 移除旧的 `0x2F8 byte5 UI_userSpeedOffset` 重发路径，避免高频源帧覆盖导致偏移不稳定；复用 1.0.9 已有自动目标表、绝对上限和可配置降速平滑引擎，不引入重复算法。
- 项目稳定版本格式同时接受两段版本（如 `1.10`）和既有三段版本，自动发布标签为 `v1.10-atlas-single-can`。

### Safety
- 速度偏移不再拥有独立 TX 路径，只能随通过现有 CAN/FSD enable、OTA、AP-First、Instant Engage、Soft Engage、`checkAD`、Abort Guard 与插件所有权检查后的 `0x3EE mux0` 一起发送；智能模式仍默认关闭。
- 已完成 native 与 Dashboard/API 契约测试和 Waveshare ESP32-S3 16MB release 构建；尚未进行实车道路闭环验证，首次使用应先观察 `legacySpeed.limitSource` 与 `mux0RxByte3/mux0TxByte3`。

## [1.0.9] - 2026-07-14

### Added
- **四模式 Legacy NAG 选择器**：稳定保留 `0=Off`、`1=Human Replay TSL6P`、`2=EPAS Late Echo`，并恢复 `3=Reactive Sustained Hold`。Reactive Hold 包含 proactive 与 reactive 两个阶段；Dashboard 桌面/手机界面、NVS、备份/恢复和统一运行时诊断均使用同一模式编号。
- **Instant Engage（实验，默认关闭）**：新增 `ap_first_edge` 配置（NVS `apfe`）和桌面/手机同步开关。仅在 primary Legacy AP 状态出现真实的 non-engaged → engaged 边沿时，一次性绕过已配置的 AP-First 延迟；父 AP gate 关闭时保留已保存的 Instant Engage 偏好。
- **AP-First 运行证据**：`/status.fsdDiag.gate` 与串口 `system_status` 现报告 Instant Engage 有效状态、AP engaged 状态、边沿次数、最近边沿年龄、pending/debounce 状态和实际 debounce bypass 次数。
- **Flash artifact checker**：发布前统一验证 generated sdkconfig、ESP image header、`flasher_args.json`、partition binary、OTA data、app slot、8 项 release bundle、merged offsets 与 SHA256。

### Changed
- Legacy AP 状态语义统一为：state `2` 不算 engaged，只有 `3..6` 算 engaged；`8/9`、disengage、CAN 关闭、OTA 阻断、父 AP gate 关闭、handler 切换或 runtime reset 会清除 transient AP-First timing。
- `bionicSteering` 保留为历史兼容的父 enable；它不是第五种 NAG 算法。实际算法由四模式选择器决定，父开关关闭时保留已保存的模式。
- ESP-IDF defaults 现与 PlatformIO 16MB 产品 profile 和双 OTA partition table 一致，不再生成 2MB/single-app metadata 或 app `0x10000` flasher args。
- Tests workflow 覆盖完整 18 个 native environments，并在 Waveshare build 后验证真实 Flash 产物。

### Fixed
- Release profile 补齐 `INJECTION_AFTER_AP`，确保发布固件与测试固件、产品文档使用相同的 AP Injection Gate 默认值。
- GitHub Release workflow 生成并验证标准 8 资产后保持 draft，不再在测试/人工批准前立即公开。
- 发布包 `flash.sh` 烧录前校验 `SHA256SUMS`，并明确 merged flash 会覆盖 NVS、`--split` 用于保留 NVS/SPIFFS 的常规升级。

### Safety
- Instant Engage **只**绕过 Legacy `0x3EE mux0` 的可配置 AP-First settle delay（本地默认仍为 `2000ms`）；不会绕过 CAN/FSD enablement、OTA blocking、父 AP gate、`checkAD`、gear logic、Abort Guard、Soft Engage、插件或功能自身开关。
- v1.0.9 候选完成本地自动化、release-equivalent clean build 与资产校验；**尚未 push、tag、公开 Release、flash、OTA 或进行台架/车辆测试**。
- 历史事故与旧禁令文档继续作为背景材料保留，但不再构成对当前 opt-in `0x370` NAG 模式的 blanket ban；当前安全边界以模式选择、父开关和既有门控为准。

## [1.0.8] - 2026-07-09

### Fixed
- **AP-First engaged-only 安全热修**：对齐上游 `flipper-tesla-fsd v2.16-beta.15` 的核心语义，补强 DAS AP state 回归测试：`0/1/2/7/8/9/15=false`，仅 `3/4/5/6=true`。Legacy `0x3EE` 在 AP available/off（state 2）时不注入；从 engaged 进入 abort/fault（8/9）会清除 stale `APActive` 并 fail-closed；state 3 与 CN 2026.8.3.6 的 state 6 正常通过既有门控。
- **诊断时间戳 underflow**：`dashAgeMs(now, 0)` 现在返回安全数值 `0`，Dashboard `lastRecoveryAgeMs` 统一走该 helper，避免缺失事件显示 `4294964s` 或其它 unsigned wraparound 值。

### 安全
- 本次为最小热修移植：未引入上游 Black-box recorder、Tap Check、Signal Map、Nag Burst、Profile Suggest、Body-bus reachability 等大功能；未改变 UI 布局、NVS 设置、发布资产结构或任何 `0x370` 注入能力。

## [1.0.7] - 2026-07-05

### Fixed
- **手机底部标签栏切换后空白**：在硬件 / 速度 / 网络 / 防护页（长内容滚动页）切换后，底部标签栏图标与文字消失只剩空栏，无法准确切换。根因是 `#mob-tabs` 等 `position:fixed` 元素嵌套在 `.content`（`overflow-y:auto` 滚动容器）内，移动端浏览器（iOS Safari 等）在 momentum scroll 时无法重绘 fixed 后代。修复：`DOMContentLoaded` 调用新增的 `hoistMobileChrome()`，把 `mob-tabs / mob-more-single / mob-tabs-dual / mob-more / disclaimer-overlay` 五个 fixed 元素提升为 `<body>` 直接子级（fixed 底部导航的 canonical 模式），并给 `#mob-tabs` 补 `data-dual-hide="1"` 使其脱离原 wrapper 后仍由 `applyProductMode` 自洽控制显隐。驾驶页（短内容不滚动）原本就不触发，故一直正常。新增契约测试 `test_mobile_tab_bar_hoist_contract.py`（5 用例）。
- **插件上传误报「文件名过长」**：上传 `bypass-tlssc+fsd-hw4.json` 等插件时报 "plugin name too long"，实际并非文件名——后端限制的是插件 JSON 内的 `"name"` 字段长度（原上限 31 字符，描述性名称易超）。前端发 `application/octet-stream` 不传输文件名，错误信息与文件名拼在一起 `安装失败：{文件名} plugin name too long` 造成误解。修复：`kDashPluginMaxNameLen` 常量化并放宽到 63（`std::string` 无固定 buffer，可安全提高）；错误信息改为 `plugin "name" too long (max 63 chars, got N)` 明确指向 JSON name 字段并显示实际长度；`/plugins/status` 的 `limits` 增加 `maxNameLen`。native 测试更新为边界覆盖（63 接受 / 65 拒绝）。

### 安全
- 标签栏修复为纯前端（HTML/JS），后端零改动；插件名长度修复仅触及 `dash_plugin_engine.h` 的校验逻辑（`name` 字段长度上限），不涉及 CAN 帧写入、`0x370` 注入或任何安全约束。所有 v1.0.6 硬约束（EPAS 注入事故禁令、`0x370` 回声/伪造注入禁令等）完全不变。

## [1.0.6] - 2026-07-03

### Added
- **触摸 UI 统一（车机/手机触摸屏可用）**：FSD 防护 / 速度策略 / 驾驶状态页里 4 个桌面 `<select>`（NAG 模式、AP 延迟注入×2、Legacy 智能速度偏移模式）改为 `sel-cards` 大卡片；7 个 `<input type=number>`（手动偏移、平滑速率、Legacy 速度偏移、4 个自定义百分比）改为 Stepper 步进器（含长按加速、min/max 钳位变灰、紧凑变体适配 4 列网格）。退化衔接：保留隐藏原生控件承载数值，外裹触摸 UI，按钮改值后派发 `change` → 现有 `saveXxx()/loadXxx()` 零改动。
- **强制免责声明弹窗**：每次打开车机/手机 UI 强制弹出，含研究/教学用途与风险告知（车辆故障、安全事故、保修失效、保险拒赔）+ Telegram 频道（`t.me/+PKsCVABYQTdkZGQ1`）+ X（`@Jordanjordan88`）+ 作者 ATLAS，点「确认」关闭（`sessionStorage` 本会话不重弹）。
- i18n：新增总开关 / 免责声明的英文条目。
- 契约测试 `test_ui_touch_unify_contract.py`（23 用例）锁防 UI 转换回退。

### Fixed
- **FSD 防护总开关 wiring bug**：`saveDefenseConfig()` 的 POST `enabled` 字段原误接 `hw3-slew-tgl`（"启用 slew rate 限制"开关），导致 slew 开关被错误兼用为防护总开关。新增独立 `def-master-tgl` 总开关（FSD 防护卡顶部），拨正 `enabled` 来源到它；`loadDefenseConfig` 从 `/defense_config` 的 `enabled` 字段回填总开关状态。
- 审查发现的 4 个转换 bug：Stepper `data-dir` 单字符符号解析（原 `parseInt` 致 NaN）、AP 延迟卡片 `<label class=field>` 无效嵌套、Legacy 速度偏移 stepper 视觉不同步（改走 `setVal`）、免责弹窗确认按钮必须在 `DOMContentLoaded` 内绑定（顶层 IIFE 在 overlay DOM 解析前执行，监听绑不上导致弹窗关不掉）。

### 安全
- 后端 C++ 零改动（纯前端 UI 重构，`include/web/mcp2515_dashboard.h` 等后端文件未触及）。
- EPAS 注入事故禁令、`0x370` 回声/伪造注入禁令、所有 v1.0.5 硬约束（无新增 `0x399/0x331/0x3F8` 写、未扩大 `0x370`）完全不变。

## [1.0.5] - 2026-07-02

### Added
- **EPAS Late Echo（研究模式转正）**：Legacy handler 内的 EPAS late-echo NAG 引擎。tokenized TX 完成、in-flight 帧所有权、fresh DAS context gate、AP9 abort 短路、torque walk、defense config 联动、UI 选择器 + 诊断。默认关闭，opt-in（NVS / defense_config）。从 v1.0.4 pre-release（`v1.0.4-atlas-single-can-epas-late-echo`）转正进入稳定版。
- **Legacy 智能速度偏移（Smart Offset）**：Off / Manual / Auto / Custom 四模式。Auto 按限速分段百分比 + 绝对 cap 表（35→60 … 120→132）+ 降速平滑（默认 5 km/h/s，含 sub-kph 高频累积修复）。**输出唯一路径**：`0x2F8 / 760 UI_gpsVehicleSpeed byte5 low 6 bits`，保留 byte5 高 2 位（`0xC0`）。Dashboard/API/UI/status/export/import/serial 全集成；NVS keys `lo_mode/lo_smooth/lo_rate/lo_p1..4`。
- **Abort Guard（实验，默认关闭）**：对齐 `flipper-tesla-fsd` v2.16-beta.11 steer-jerk probe。AP state 8/9 latch 后 block 注入；state <2 clear；state 6 不 clear。覆盖 Legacy 0x2F8 / 0x3EE mux0·mux1 / 0x438 / NAG late-echo / plugin engine / AP auto-restore / HW3·HW4 send paths / HW4 923 latch-before-gate。
- **HW4 `0x39B/923` 安全 decoder**：标准 HW4 从 byte1 high nibble 解 AP state；Highland byte0 fallback 仅在 pinned-byte1 signature 连续 3 帧 candidate 后启用；byte1 再次移动即永久退出 fallback（保守且正确）。NVS key `def_ag`。
- native 测试：HW4 923 fallback 状态机（标准 byte1 latch / byte0 false-latch 抑制 / 连续 candidate / fallback 退出）、Abort Guard 状态机、Smart Offset 全模式 + 平滑累积；Python 契约（block-scoped 禁写扫描、760 byte5 单一字段写入、opt-in 门控、默认值）。

### 安全
- 硬约束逐项通过 final review：无新增 `0x399 / 0x331 / 0x3F8` 写；无新增 `0x3FD non-HW4` offset 写；未扩大 `0x370` torque/hands-on 注入（torque tamper 仍 opt-in/default off）；智能速度输出仅 `0x2F8/760 byte5 low 6 bits`。
- Abort Guard 与 Smart Auto 升级后默认关闭（升级最多进入 Manual，不会自动选 Auto）。
- EPAS 注入事故禁令完整（`test_no_epas_nag_contract.py` 守卫不变）。

## [1.0.4] - 2026-06-25

### Added
- **Soft Engage（方向盘居中门控）**：对齐上游 `flipper-tesla-fsd` v2.16-beta.10。Legacy `0x3EE bit46` FSD 激活注入在 AP 稳定后额外 hold 到方向盘近居中（±5°，`SOFT_ENGAGE_ANGLE_THRESH_X10=50`）或 5s 超时（`SOFT_ENGAGE_TIMEOUT_MS=5000`）再注入，episode 内锁存——直接降低弯道激活边沿的转向猛甩（V1.0.3 AP-settle 门控的角度细化，解释"弯道猛甩更严重"=偏心时 DAS 路径重算大）。默认 ON；UI「Soft Engage 方向盘居中」开关可关（NVS `def_se`，OFF=精确回退 V1.0.3 行为）。±5°/5s 为保守默认，实车可调。纯 `0x3EE` 侧，不涉 EPAS/`0x370`。角度判定抽成纯函数 `dashSoftEngageRelease`（`can_helpers.h`）以支持 native TDD。
- **NagHandler 双模式**：默认 PASSTHROUGH（0x370 扭矩字节直通不篡改），opt-in TORQUE_TAMPER（1.80Nm 固定扭矩 `0xB6`，UI「扭矩篡改」开关 + NVS `def_ntt`，默认关 + 「高危/严禁上车」警告）。生产构建 NAG_KILLER off → NagHandler 不注册 → 双模式运行时死代码（纵深防御：即使 opt-in 被翻 true 也不发包）。
- NagHandler 真实 0x370 帧台架回放测试（1256 帧，counter 0 碰撞 + echo 整形验证）。
- native 测试：`dashSoftEngageRelease` 纯函数 10 测试（含负角度右转符号约定守卫 `abs()`）；Soft Engage 全链路接线 Python 契约。

### Changed
- 移除 NagHandler bionic 正弦扰动路径（被双模式取代）；契约测试同步更新为断言双模式（passthrough 默认 + tamper opt-in），并增加 `DashBionicSteer`-stays-out 守卫。

### Fixed
- 修正 Phase-1 遗留的 stale NagHandler 契约测试（断言已移除的 bionic 路径，致 suite 1 failing）。

### 安全
- EPAS 禁令完整：8 个 `DashEpasNag` 禁用符号仍 0 命中（`test_no_epas_nag_contract.py` 守卫不变）。

## [1.0.3] - 2026-06-23

### Fixed
- **CN 2026.8.3.6 方向盘猛甩（AP 注入激活边沿）**：`isDASAutopilotActive` 此前不认 DAS state 6（该固件的 AP 接入状态），导致 `APActive` 恒 false、AP-First 门控无法 arm。现认 state 6（3–6 视为 active，8 handover / 9 fault 仍 inactive）。
- **AP-First 门控默认开启**：waveshare 构建现启用 `INJECTION_AFTER_AP`，Legacy `0x3EE` 注入延迟到 AP 稳定 ~2s 后，避开激活边沿的 `0x488` 转向指令暴跳（数据： jerk 日志在 AP 接入后 +0.2s 出现 Δ179–248°）。**降低但无法消除**该风险（上游残留 <5%）。

### Changed
- **AP 门控用户可关闭**：非 8.3.6 车型可在仪表盘「AP 门控」开关关闭以直接注入（跳过 AP-First 等待）。开关 NVS 持久化（`ap_gate`），UI 写清「ON=防 8.3.6 猛甩（默认）/ OFF=直接注入（非 8.3.6）」。

### Added
- native 回归测试：门控关闭时 Legacy `0x3EE` 无需 AP 直接注入；`isDASAutopilotActive(6)==true`。
- 上游诊断文档 `docs/steer-jerk-diagnosis-20260623.md`（含数据表 + 因果链，致 `flipper-tesla-fsd#108` / `ev-open-can-tools#66`）。
- README 与仪表盘 UI 警示：CN 2026.8.3.6 高风险 + Listen-Only 建议。

## [1.0.2] - 2026-06-23

### Fixed
- **车机（桌面）AP 延迟注入时间无法选择**：`renderApInjectionState` 用 `||` 回填导致选 0ms 被覆盖回 2000，且定时 poll 回填与用户选择 race。改为 `!=null` 判断 + select 聚焦时不自动回填，用户选择不再被覆盖。
- **手机 UI 新增 AP 延迟注入设置**：原先仅在桌面「AP 注入安全」卡片，手机版遗漏。在「FSD 防护」页（手机"更多"可达）新增 AP 延迟注入卡片，桌面/手机用 `.ap-delay-select` class 统一回填与保存。
- **手机横屏底部菜单按钮消失**：媒体查询断点 `max-width: 768px` 导致手机横屏（宽度 > 768）`.mob-tabs` 被隐藏。断点提高到 `1024px`，覆盖手机横屏与平板。

## [1.0.1] - 2026-06-21

First open-source release of the Waveshare single-CAN standalone firmware.

### Added
- `waveshare_single_can_standalone` profile for Waveshare ESP32-S3-RS485-CAN single-CAN deployments.
- Dashboard capabilities for single-CAN UI/API pruning: CAN2, Bus2, Service Mode, Stalk/highbeam, and lighting-stunt surfaces are hidden or unregistered in standalone builds (controlled via the `DASH_SINGLE_CAN_STANDALONE` build flag).
- AP Injection Gate safety parity with upstream `ev-open-can-tools v3.0.2-beta.3`: Legacy `0x3EE mux0` requires AP active for 2 seconds when the gate is enabled, and live unknown gear fails closed.
- Official JSON plugin manager for Waveshare Single CAN standalone: URL install, `.json` upload, paste JSON, disabled-by-default installs, per-plugin enable/priority/remove/detail, GTW2047 replay count, and SPIFFS persistence (`/plugins_state.json`) restored on boot.
- AP Gate delayed-injection controls and AP injection state to the standalone driving status UI (desktop + mobile).
- `GET /config` route so the UI reloads persisted FSD runtime state (legacy offset / override speed limit) on reconnect.
- Factory-default WiFi credentials (`Atlas-FSD` / `12345678`) and OTA password baked into the open-source build; README and `flash.sh` prompt users to change them before real deployment.
- OTA update channel pointed at the public release repo (`JordanzhaoD/waveshare-single-can-firmware`).

### Changed
- Cleaned standalone desktop and mobile UI to remove CAN2/T-2CAN/Auto Shift product surfaces while preserving dual-CAN builds.

### Fixed
- Corrected the firmware version label shown in the vehicle-status panel from `4.1.0` (inherited from the upstream T-2CAN project) to the correct `1.0.1`. The `VERSION` file now drives the dashboard `uiBuildId`.

## [4.0.2] - 2026-05-31

### Added
- LILYGO T-2CAN dual-bus firmware is now included in CI/release workflows as `firmware-lilygo-t2can-dual.bin`.
- Auto-shift placeholder page now displays read-only gear-assist telemetry from `/gear_assist_status`.

### Changed
- FSD/CAN injection safety now gates handler-level injection through the OTA guard and keeps LILYGO release builds injection-off on boot.
- HW3 speed-offset SNA/NONE handling now preserves incoming mux-2 payload and clears offset telemetry.
- Fog/strobe control now sends a final OFF frame from the CAN task when stopped or when gear is unsafe/stale.
- Wheel DND settings are persisted and re-arm once when the defense gate opens.

### Fixed
- Power-management partial updates preserve omitted settings and use configurable wake pin selection.
- Bionic steering status/re-enable path now reads and resets the active handler state.
- Dashboard drive cards preserve Auto/Sloth/MAX state instead of collapsing to legacy speed profiles.
- Chip temperature display uses the red error style above 60°C.

## [3.0.4-beta.1] - 2026-05-28

### Changed (Breaking)
- FSD detection bit corrected from 37 to 38 (matches verified tesla-fsd-controller)
- All compile-time feature flags (ISA_SPEED_CHIME_SUPPRESS, EMERGENCY_VEHICLE_DETECTION,
  ENHANCED_AUTOPILOT, BYPASS_TLSSC_REQUIREMENT, DASH_FSD_252_COMPAT) replaced with
  runtime switches accessible from Dashboard UI
- LegacyHandler CAN 760: replaced MPP bucket table with simple offset write
- HW3Handler mux 2: complete rewrite with auto target, custom buckets, high-speed mode

### Added
- Auto hardware detection mode (hwMode=3): detects HW3/HW4 from CAN 920
- Ban Shield: CAN 2047 snapshot protection (opt-in)
- TLSSC bypass option (bit 38) for HW3+HW4
- HW4 speed offset support (hw4OffsetRaw) on mux 2
- Legacy CAN 1080 visionSpeedSlider override
- Legacy CAN 1006 mux 1 removeVisionSpeedLimit option (bit 48)
- HW3 auto speed targeting: <60->64kph, =60->100kph, 60-79->85kph, >=80->passthrough
- 15 new CarManagerBase state fields for runtime FSD configuration

and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0-beta.5] - 2026-05-04

### Added

- RGB status LED now reflects runtime state at a glance: solid green = injecting + at least one client connected, blinking green = injecting with no client connected, solid red = injection stopped + connected, blinking red = injection stopped + no client. Solid blue indicates an in-progress OTA firmware update. "Connected" is true when any station is associated with the AP or when the STA uplink is up.

## [3.0.0-beta.4] - 2026-05-04

### Added

- Manual firmware upload now has a dashboard button to clear saved OTA username/password credentials from browser local storage.

## [3.0.0-beta.3] - 2026-05-04

### Added

- Plugin rules can now include an additional byte-mask match (`match_byte`, `match_mask`, `match_val`) so plugins can target specific bit states without adding dedicated firmware toggles.
- Added an example `0x370` duplicate-counter plugin that matches byte 4 bits 7:6 clear, forces byte 3 to `0xB6`, sets byte 4 bit 6, increments byte 6 low-nibble counter, and recomputes byte 7 checksum.

### Fixed

- ESP-IDF WiFi Internet saves now defer reconnect until after the HTTP response, preventing the dashboard request from being dropped while AP+STA mode changes channels.
- Switching saved WiFi networks now disconnects any existing STA association before connecting to the new SSID, avoiding repeated `sta is connected` / deauth log spam.
- WiFi scan now prepares AP+STA mode before scanning, so scans work when the device is otherwise in AP-only mode.
- ESP-IDF WiFi/httpd/netif component logs are reduced to warning/error levels to keep dashboard serial logs readable.

## [3.0.0-beta.2] - 2026-05-04

### Changed

- Pin ESP-IDF builds to ESP-IDF v6.0.1 through PlatformIO `platformio/espressif32` 7.0.0 and keep legacy Arduino boards in `legacy-arduino`.
- Build all supported ESP-IDF and legacy Arduino targets in GitHub Actions, including release artifacts for AtomS3 Mini CAN Base and Waveshare ESP32-S3 RS485/CAN.

### Fixed

- Release and test workflows now run legacy Arduino board builds from `legacy-arduino`, so RP2040 and Feather M4 CI no longer look for removed root Arduino environments.
- CI installs the Python package needed by ESP-IDF 6.0.1 tooling and skips clang-format on the generated dashboard payload header.

## [3.0.0-beta.1] - 2026-05-03

### Added

- ESP-IDF 5.5 native build path replacing the Arduino framework on supported targets (M5Stack AtomS3 Lite verified). Adds `src/espidf_runtime.cpp` plus `include/platform/espidf_runtime.h` providing Arduino-compatible shims (`String`, `WiFi`, `WebServer`, `Preferences`, `Update`, `HTTPClient`, `SPIFFS`, etc.) on top of ESP-IDF APIs.
- ESP-IDF `led_strip` (RMT) implementation of the on-board RGB status LED so the AtomS3 Lite indicator works under IDF (green = injecting, red = idle).
- "Status LED Brightness" subsection in the Configuration card with a slider and number input (0–255), persisted in NVS (`led_b`) and applied live via `/led_brightness`.
- Multi-SSID WiFi support — up to 4 saved networks. The device tries each in turn until one connects (e.g. home + phone hotspot). New endpoints `/wifi_networks`, `/wifi_delete`; `/wifi_config` accepts an `idx` argument to update a specific slot. Legacy single-SSID NVS keys auto-migrate to slot 0 on first boot.
- `scripts/minify_dashboard.py` — minifies the dashboard HTML/CSS/JS (csscompressor + terser + htmlmin) and emits a gzipped `DASH_HTML_GZ[]` payload served with `Content-Encoding: gzip`. Dashboard source-of-truth moved to `include/web/mcp2515_dashboard_ui.src.h`.

### Changed

- Flash usage on `m5stack-atoms3-mini-can-base` reduced from 79.0 % (1,243,217 B) to 64.8 % (1,019,835 B) — about 223 KB saved with no feature loss. Drivers:
  - `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (was `OPTIMIZATION_DEBUG`),
  - `CONFIG_NEWLIB_NANO_FORMAT=y` plus integer formatting for the FPS field,
  - `CONFIG_ESP_ERR_TO_NAME_LOOKUP=n`,
  - `CONFIG_BOOTLOADER_LOG_LEVEL_ERROR=y`,
  - gzipped dashboard HTML (≈120 KB raw rodata → ≈30 KB compressed).
- `WebServer::send_P` no longer copies the body into a `String` (which OOM'd on the 130 KB dashboard HTML and aborted the httpd task). It now streams large responses in 4 KB chunks via `httpd_resp_send_chunk` directly from the source pointer (`sendRaw`).
- `WebServer::begin()` bumps the IDF httpd task stack to 16 KB and moves the per-request URL-query buffer to the heap, fixing a stack overflow on the first dashboard hit.

### Fixed

- `httpd` task stack overflow on the first client connection on ESP-IDF builds.
- `bad_alloc` / `terminate` reboot when serving the root dashboard page over the soft-AP on ESP-IDF builds.

## [2.6.0-beta.1] - 2026-04-30

### Added

- Added MCP2515 recovery regression coverage for the RP2040 CAN driver.
- Added `platformio_profile.example.h` as the committed build-config template.

### Changed

- `platformio_profile.h` is now treated as a local-only build config and ignored by git. Copy `platformio_profile.example.h` to `platformio_profile.h` before building, then keep board choices, credentials, and keys in the local file.
- Build documentation and helper-script errors now describe `platformio_profile.h` as the local build config and point fresh checkouts to the example file.

### Fixed

- RP2040 MCP2515 builds now recover the CAN controller after repeated TX failures or MCP2515 bus-off (`EFLG_TXBO`) instead of staying silent until power-cycled.
- CI test and release builds now create their local `platformio_profile.h` from `platformio_profile.example.h` before applying per-board build profiles.

## [2.5.2] - 2026-04-29

Stable release bundling the AP Injection Gate Smart Summon fixes from `2.5.2-beta.1` through `2.5.2-beta.6`.

### Fixed

- AP Injection Gate no longer drops during Smart Summon launches that report `DI_autonomyControlActive` while still in Park before an immediate turn. Definite Park frames now clear the summon latch only when ACA is inactive, so the earlier non-zero `UI_selfParkRequest` survives the shift out of Park even when no fresh request frame appears after the shift.

## [2.5.2-beta.6] - 2026-04-29

### Fixed

- AP Injection Gate no longer drops during Smart Summon launches that report `DI_autonomyControlActive` while still in Park before an immediate turn. Definite Park frames now clear the summon latch only when ACA is inactive, so the earlier non-zero `UI_selfParkRequest` survives the shift out of Park even when no fresh request frame appears after the shift.

## [2.5.2-beta.5] - 2026-04-28

### Fixed

- AP Injection Gate no longer stays open for 3-5 s after Autopilot is disengaged, and no longer treats plain TACC as an open-gate condition. `DI_autonomyControlActive` (ACA) on CAN 280 is set during AP, TACC, *and* Smart Summon, so it cannot drive Summoning by itself. Summoning now requires both `ACA=1` *and* a `UI_selfParkRequest` non-zero command observed in the current autonomy episode. ACA falling edge resets the spr-seen flag, so a subsequent TACC engagement does not re-latch the gate. The 5 s ACA-only timeout is removed: AP disengage drops ACA immediately and the gate closes immediately, restoring instant AP / TACC re-engagement.

## [2.5.2-beta.4] - 2026-04-28

### Fixed

- AP Injection Gate now opens on a freshly-booted module when the car is asleep / locked with Sentry. While the DI is asleep, CAN ID 280 (`DI_systemStatus`) is not broadcast at all (only DAS / autopilot ECU IDs 921/1016/1021/2047 keep transmitting), so the previous boot-time `Parked=false` default left the gate stuck at `Waiting AP` until the driver pressed the brake to wake the DI. `Parked` now defaults to `true` at boot; the first `DI_systemStatus` frame with a driving gear (R/N/D) flips it to false. If the DI never reports, the car is asleep / parked and the gate remains open by design.

## [2.5.2-beta.3] - 2026-04-28

### Fixed

- AP Injection Gate now stays open for the full duration of a Smart Summon / Smart Park session. Detection now uses `DI_autonomyControlActive` (CAN ID 280, bit 50 / byte 6 bit 2) as the primary "summon active" signal in addition to `UI_selfParkRequest` on CAN 1016. The DI bit is held high for the entire time the car is being driven by an autonomy stack, so the 5 s spr-only timeout no longer expires mid-summon when the UI command pulse drops to 0 while the car keeps driving itself.

## [2.5.2-beta.2] - 2026-04-28

### Fixed

- AP Injection Gate no longer drops while Summon shifts the vehicle to Reverse. `clearSummonOnPark()` now fires only on a definitive `DI_gear == 1 (P)` value, not on the permissive `isVehicleParked` set that also includes `0=INVALID` and `7=SNA`. SNA can blip during gear transitions (e.g. P->R under Summon control), and the previous logic would clear `Summoning` on that blip and close the gate mid-summon. Shift to Drive was unaffected because `selfParkRequest` stayed non-zero long enough to re-latch `Summoning`; Reverse was reaching gear faster than the latch could recover.

## [2.5.2-beta.1] - 2026-04-28

### Fixed

- AP Injection Gate now opens while the car is asleep / locked with Sentry. `isVehicleParked` now treats `DI_gear` values `0=INVALID` and `7=SNA` as parked in addition to `1=P`. When the DI is asleep it reports SNA on CAN ID 280, which previously left `Parked=false` and kept the gate stuck at `Waiting AP` until the driver pressed the brake to wake the DI. Driving states (R=2, N=3, D=4) are still not parked, so the gate behavior on a moving car is unchanged.

## [2.5.1] - 2026-04-28

Stable release bundling all changes from 2.4.2 onwards (`2.5.0-beta.5` through `2.5.0-beta.12`). Notably, the `Start after AP` dashboard toggle now gates plugin injection on AP, Park, *and* Summon / Smart Park, which makes the firmware compatible with vehicles running Tesla software release **2026.14.3** and newer — these versions reject the always-on injection used by earlier dashboards, so the toggle must be enabled to keep injection working on those vehicles.

### Added

- Dashboard speed profiles now include an `Auto` mode. Auto follows the vehicle follow-distance selection, while a manual dashboard profile stays locked and is injected instead of being overwritten by the car.

### Fixed

- Legacy speed profile selection is now written back into outgoing CAN ID `1006` mux `0` frames so the selected profile actually takes effect on vehicle behavior instead of only changing the observed internal state. (rolled up from 2.4.2-beta.1)
- AP Injection Gate now detects active AP from recorded HW3 `1021` mux `0` frames by reading the observed AD bit, so plugin injection no longer stays stuck at `Waiting AP` after Autopilot is engaged.
- AP Injection Gate now waits for DAS `AutopilotStatus` active states instead of the 1021 UI/config bit, preventing plugin injection from switching to Active when AP is not engaged.
- OTA update finalize now passes `true` to `Update.end()` to force completion regardless of residual byte count, fixing the `Update finalize failed` error that could occur after a successful download via GitHub S3 redirects.
- OTA error paths now log `Update.errorString()` at every failure point (begin, write, finalize) so the root cause is visible in the dashboard log instead of a generic message.
- AP Injection Gate now also opens while the vehicle is in Park, so Summon unlock injection can run while parked and stops again after shifting to Drive.
- Dashboard manual profile selection now persists in firmware state and is applied to Legacy, HW3, and HW4 injection paths.
- AP Injection Gate park detection now also reads `DI_systemStatus` (CAN ID 280) `DI_gear`, so the gate reopens when the vehicle is shifted to Park on Chassis-bus connections that do not carry `DIF_torque`/`DIR_torque` (CAN ID 390). MCP2515 hardware filter slots updated for Legacy, HW3, and HW4 modes to admit ID 280.
- AP Injection Gate now also stays open while Summon / Smart Park is active. HW3 and HW4 handlers parse `UI_driverAssistControl` (CAN ID 1016) `UI_selfParkRequest` (byte 3 bits 4-7); when the request is non-zero (4=PRIME, 5=PAUSE, 7/8=AUTO_SUMMON_FWD/REV, 11=SMART_SUMMON), `Summoning` is asserted and held for 5 s after the last activity, so injection keeps running once the vehicle shifts out of Park into Drive/Reverse under Summon control.
- AP Injection Gate `Summoning` flag is force-cleared whenever the vehicle returns to Park, so a manual P->D shift after a completed summon correctly waits for AP again instead of latching the gate open until reboot.

## [2.5.0-beta.12] - 2026-04-27

### Fixed

- AP Injection Gate Summon detection no longer latches `Summoning` permanently after the first summon use. `UI_summonHeartbeat` is no longer used for activity tracking because it keeps cycling 0..3 indefinitely once summon has run, which prevented the gate from ever closing again. Detection now relies solely on `UI_selfParkRequest` (CAN 1016, byte 3 bits 4-7); the flag holds for 5 s after the last non-zero command and is also force-cleared whenever the vehicle returns to Park, so a manual P->D shift after a completed summon correctly waits for AP again.

## [2.5.0-beta.11] - 2026-04-27

### Fixed

- AP Injection Gate now also stays open while Summon / Smart Park is active. HW3 and HW4 handlers parse `UI_driverAssistControl` (CAN ID 1016) `UI_summonHeartbeat` (byte 0 bits 2-3) and `UI_selfParkRequest` (byte 3 bits 4-7); when either is non-zero, `Summoning` is asserted and held for 1500 ms after the last activity, so injection keeps running once the vehicle shifts out of Park into Drive/Reverse under Summon control.

## [2.5.0-beta.10] - 2026-04-27

### Fixed

- AP Injection Gate park detection now also reads `DI_systemStatus` (CAN ID 280) `DI_gear`, so the "Start after AP" gate reopens when the vehicle is shifted to Park on Chassis-bus connections that do not carry `DIF_torque`/`DIR_torque` (CAN ID 390). MCP2515 hardware filter slots updated for Legacy, HW3, and HW4 modes to admit ID 280.

## [2.5.0-beta.9] - 2026-04-27

### Added

- Dashboard speed profiles now include an `Auto` mode. Auto follows the vehicle follow-distance selection, while a manual dashboard profile stays locked and is injected instead of being overwritten by the car.

### Fixed

- Dashboard manual profile selection now persists in firmware state and is applied to Legacy, HW3, and HW4 injection paths.

## [2.5.0-beta.8] - 2026-04-27

### Fixed

- AP Injection Gate now also opens while the vehicle is in Park, so Summon unlock injection can run while parked and stops again after shifting to Drive.

## [2.5.0-beta.7] - 2026-04-25

### Fixed

- OTA update finalize now passes `true` to `Update.end()` to force completion regardless of residual byte count, fixing the `Update finalize failed` error that could occur after a successful download via GitHub S3 redirects.
- OTA error paths now log `Update.errorString()` at every failure point (begin, write, finalize) so the root cause is visible in the dashboard log instead of a generic message.

## [2.5.0-beta.6] - 2026-04-24

### Fixed

- AP Injection Gate now waits for DAS `AutopilotStatus` active states instead of the 1021 UI/config bit, preventing plugin injection from switching to Active when AP is not engaged.

## [2.5.0-beta.5] - 2026-04-24

### Fixed

- AP Injection Gate now detects active AP from recorded HW3 `1021` mux `0` frames by reading the observed AD bit, so plugin injection no longer stays stuck at `Waiting AP` after Autopilot is engaged.

## [2.5.0] - 2026-04-24

First stable release of the 2.5 series. Bundles all changes from 2.5.0-beta.1 through 2.5.0-beta.4.

### Added

- Dashboard configuration now includes a GTW 2047 plugin replay control, and settings backup/import now preserves plugin replay preferences.
- Dashboard configuration now includes an AP Injection Gate toggle that arms plugin injection but waits until AP/NoA is observed active before sending plugin frames.
- Plugin rules now support `counter` fields and `emit_periodic` for cached GTW mux 3 broadcasts, including editor support and updated plugin documentation.
- Plugin rules now support a `bus` field (`CH`, `VEH`, `PARTY`, comma-separated string, bitmask, or array) to restrict matching to specific CAN bus pins; frames with unknown bus still match for backwards compatibility.
- Plugin rules now support a `mux_mask` field (alias `muxMask`) to control which bits of byte 0 are compared for mux matching; values 0-7 default to low-3-bit mask, values 8-255 default to full-byte mask, enabling low-nibble and full-byte DBC mux styles.
- Plugin list API now includes `bus` and `mux_mask` per rule so the dashboard UI and support exports reflect full rule configuration.
- Plugin editor gained bus and mux-mask input fields per rule, and the rule label, summary, and conflict panel now show bus pin and mux/mask.
- GTW periodic emit can now optionally try to silence native gateway broadcasts through a UDS diagnostic sequence using extended session, SecurityAccess, `CommunicationControl`, and `TesterPresent`.
- HW3 dashboard builds now expose an optional offset slew limiter for plugin-driven mux 2 offset changes.
- Added the shared `INJECTION_AFTER_AP` build option for behaviour-option builds; `ENHANCED_AUTOPILOT` mux 1 injection now waits for AP to be active when this option is enabled.
- Added a WiFi dashboard regression test that covers the WiFi settings UI, backend routes, status payload, and backup fields.
- Added native PlatformIO test environments `native_plugin_engine` and `native_plugin_engine_custom_key` for running plugin engine unit tests without hardware.

### Changed

- Plugin rules now allow up to 16 operations per rule instead of 8.
- `PLUGIN_FILTER_IDS_MAX` raised to 32 (was equal to `PLUGIN_RULES_MAX` = 16) and made overridable at build time.
- Replayed GTW frames, periodic emits, and repeated Rule Test sends now advance counter fields and refresh checksums between sends.
- Dashboard plugin details, validation, support exports, and docs now describe the new replay, counter, and periodic emit behavior.
- `gtw_silent: true` is now silently treated as disabled at parse time unless `PLUGIN_GTW_UDS_CUSTOM_KEY` is defined at build time; the periodic emit still works but no UDS sequence is started and `0x684`/`0x685` filter IDs are not injected.
- UDS request frames sent by the GTW silencing state machine are now tagged with the bus of the frame that seeded the periodic emit cache.
- Incoming frames with `CAN_BUS_ANY` are now normalized to `CAN_BUS_DEFAULT` in the main app loop before being passed to the plugin engine.
- Plugin rule mux matching and test-rule matching refactored into shared `pluginRuleMatchesBus` / `pluginRuleMatchesMux` helpers used by both the engine and the dashboard.
- Plugin editor mux input range extended to -1-255 (was -1-7) to support full-byte DBC mux values.

### Fixed

- GTW silent-mode plugin rules now add the required UDS request/response CAN IDs to the active filter set so the diagnostic state machine can observe replies.
- The CAN analyzer now labels UDS `0x28 CommunicationControl` requests by name.
- WiFi Internet status now follows the live STA connection state reliably after connect attempts and page refreshes instead of getting stuck on `Connecting to ...` or `Not configured`.
- Corrupted WiFi SSID fragments are now ignored in saved settings and filtered from `/wifi_status` responses.
- The WiFi settings form no longer overwrites the SSID field while it is being edited, and the status header no longer depends on optional labels being present.
- Dashboard CAN sniffer and recorder buffers now clamp incoming frame DLC before copying frame data.
- Plugin mux matching now ignores zero-DLC frames instead of treating them as mux 0.
- Plugin conflict detection and detail panel now correctly account for bus mask and mux mask when determining whether two rules can affect the same frame.
- Custom-key plugin engine native tests now validate output against the configured `PLUGIN_GTW_UDS_KEY_READY` value instead of a hard-coded key byte.

## [2.5.0-beta.4] - 2026-04-24

### Added

- Dashboard configuration now includes an AP Injection Gate toggle that arms plugin injection but waits until AP/NoA is observed active before sending plugin frames.
- Added the shared `INJECTION_AFTER_AP` build option for behaviour-option builds; `ENHANCED_AUTOPILOT` mux 1 injection now waits for AP to be active when this option is enabled.

### Fixed

- Custom-key plugin engine native tests now validate output against the configured `PLUGIN_GTW_UDS_KEY_READY` value instead of a hard-coded key byte.

## [2.5.0-beta.3] - 2026-04-24

### Added

- Plugin rules now support a `bus` field (`CH`, `VEH`, `PARTY`, comma-separated string, bitmask, or array) to restrict matching to specific CAN bus pins; frames with unknown bus still match for backwards compatibility.
- Plugin rules now support a `mux_mask` field (alias `muxMask`) to control which bits of byte 0 are compared for mux matching; values 0–7 default to low-3-bit mask, values 8–255 default to full-byte mask, enabling low-nibble and full-byte DBC mux styles.
- Plugin list API now includes `bus` and `mux_mask` per rule so the dashboard UI and support exports reflect full rule configuration.
- Plugin editor gained bus and mux-mask input fields per rule, and the rule label, summary, and conflict panel now show bus pin and mux/mask.
- `PLUGIN_FILTER_IDS_MAX` raised to 32 (was equal to `PLUGIN_RULES_MAX` = 16) and made overridable at build time.
- Added native PlatformIO test environments `native_plugin_engine` and `native_plugin_engine_custom_key` for running plugin engine unit tests without hardware.

### Changed

- `gtw_silent: true` is now silently treated as disabled at parse time unless `PLUGIN_GTW_UDS_CUSTOM_KEY` is defined at build time; the periodic emit still works but no UDS sequence is started and `0x684`/`0x685` filter IDs are not injected.
- UDS request frames sent by the GTW silencing state machine are now tagged with the bus of the frame that seeded the periodic emit cache.
- Incoming frames with `CAN_BUS_ANY` are now normalized to `CAN_BUS_DEFAULT` in the main app loop before being passed to the plugin engine.
- Plugin rule mux matching and test-rule matching refactored into shared `pluginRuleMatchesBus` / `pluginRuleMatchesMux` helpers used by both the engine and the dashboard.
- Plugin editor mux input range extended to −1–255 (was −1–7) to support full-byte DBC mux values.

### Fixed

- Plugin conflict detection and detail panel now correctly account for bus mask and mux mask when determining whether two rules can affect the same frame.

## [2.5.0-beta.2] - 2026-04-24

### Fixed
- Dashboard CAN sniffer and recorder buffers now clamp incoming frame DLC before copying frame data.
- Plugin mux matching now ignores zero-DLC frames instead of treating them as mux 0.

## [2.5.0-beta.1] - 2026-04-23

### Added
- Dashboard configuration now includes a GTW 2047 plugin replay control, and settings backup/import now preserves plugin replay preferences.
- Plugin rules now support `counter` fields and `emit_periodic` for cached GTW mux 3 broadcasts, including editor support and updated plugin documentation.
- GTW periodic emit can now optionally try to silence native gateway broadcasts through a UDS diagnostic sequence using extended session, SecurityAccess, `CommunicationControl`, and `TesterPresent`.
- HW3 dashboard builds now expose an optional offset slew limiter for plugin-driven mux 2 offset changes.
- Added a WiFi dashboard regression test that covers the WiFi settings UI, backend routes, status payload, and backup fields.

### Changed
- Plugin rules now allow up to 16 operations per rule instead of 8.
- Replayed GTW frames, periodic emits, and repeated Rule Test sends now advance counter fields and refresh checksums between sends.
- Dashboard plugin details, validation, support exports, and docs now describe the new replay, counter, and periodic emit behavior.

### Fixed
- GTW silent-mode plugin rules now add the required UDS request/response CAN IDs to the active filter set so the diagnostic state machine can observe replies.
- The CAN analyzer now labels UDS `0x28 CommunicationControl` requests by name.
- WiFi Internet status now follows the live STA connection state reliably after connect attempts and page refreshes instead of getting stuck on `Connecting to ...` or `Not configured`.
- Corrupted WiFi SSID fragments are now ignored in saved settings and filtered from `/wifi_status` responses.
- The WiFi settings form no longer overwrites the SSID field while it is being edited, and the status header no longer depends on optional labels being present.

## [2.4.2-beta.1] - 2026-04-23

### Fixed
- Legacy speed profile selection is now written back into outgoing CAN ID `1006` mux `0` frames so the selected profile actually takes effect on vehicle behavior instead of only changing the observed internal state.
- Added a native Legacy regression test that verifies the selected speed profile bits are injected into the transmitted mux `0` frame.

## [2.4.1] - 2026-04-22

### Added
- Dashboard header now shows the latest observed `GTW_autopilot` state next to the selected hardware mode.
- Native dashboard regression tests now assert that dashboard handlers do not send frames, increment `framesSent`, or fire `onSend`.
- README and dashboard footer now include the support and gift information from `main`.

### Changed
- Dashboard builds no longer compile automatic CAN injection paths from Legacy, HW3, or HW4 handlers; enabled plugins are the automatic injection path.
- HW3 no longer listens for or injects Track Mode request frames.
- ESP32 dashboard builds now use a 4MB OTA partition layout with larger app slots while preserving SPIFFS storage.
- Plugin documentation now describes the dashboard's plugin-only automatic injection behavior instead of firmware overlap warnings.
- Rule Test count and interval controls now use visible labels with `1` and `100` as placeholders, while empty fields still default to one injection every 100 ms.
- Support issue flow now copies the dashboard support report to the clipboard and opens the General Issue form without prefilled title or body text.

### Fixed
- ESP32 dashboard hotspot startup is more reliable by starting the AP earlier, disabling WiFi sleep, validating saved AP/STA credentials, and falling back to AP-only mode when STA connection attempts time out.

## [2.4.1-beta.3] - 2026-04-22

### Fixed
- ESP32 dashboard hotspot startup is more reliable by starting the AP earlier, disabling WiFi sleep, validating saved AP/STA credentials, and falling back to AP-only mode when STA connection attempts time out.

## [2.4.1-beta.2] - 2026-04-20

### Changed
- Rule Test count and interval controls now use visible labels with `1` and `100` as placeholders, while empty fields still default to one injection every 100 ms.
- Support issue flow now copies the dashboard support report to the clipboard and opens the General Issue form without prefilled title or body text.

## [2.4.1-beta.1] - 2026-04-20

### Added
- Dashboard header now shows the latest observed `GTW_autopilot` state next to the selected hardware mode.
- Native dashboard regression tests now assert that dashboard handlers do not send frames, increment `framesSent`, or fire `onSend`.

### Changed
- Dashboard builds no longer compile automatic CAN injection paths from Legacy, HW3, or HW4 handlers; enabled plugins are the automatic injection path.
- HW3 no longer listens for or injects Track Mode request frames.
- ESP32 dashboard builds now use a 4MB OTA partition layout with larger app slots while preserving SPIFFS storage.
- Plugin documentation now describes the dashboard's plugin-only automatic injection behavior instead of firmware overlap warnings.

## [2.4.0] - 2026-04-19

## [2.4.0-beta.1] - 2026-04-19

### Added
- Dashboard plugin priority controls now let users choose which enabled plugin wins overlapping bit writes
- Dashboard plugin conflict warnings now show firmware overlaps, plugin priority overlaps, and the first enabled injection priority

### Changed
- Plugin installs now start disabled so users can review priority and conflicts before enabling them
- Rule Test now waits for the next matching live CAN frame, applies the selected rule to that frame, and injects the captured result with the chosen count and interval
- Dashboard polling now keeps `/status` as the connection gate and backs off non-critical polls during reconnects

### Fixed
- Plugin rules targeting the same CAN ID and mux are merged into one injected frame per incoming frame, preventing contradictory duplicate plugin sends in the same cycle
- TWAI dashboard filtering now drops sparse-mask false positives in software, reducing dashboard load when plugin CAN IDs widen the hardware mask
- Dashboard status rendering no longer breaks when optional status-grid elements are removed or rearranged
- Dashboard remains responsive under heavier CAN/plugin load by limiting per-loop frame draining and giving the web task more scheduling priority

## [2.3.2-beta.2] - 2026-04-18

### Changed
- Dashboard no longer exposes or persists speed profile control; follow distance and the derived profile remain visible as read-only status
- Dashboard profile-related boot logging and legacy NVS cleanup now reflect the new plugin-managed model without stale `SP` or profile-lock state

### Fixed
- Dashboard core no longer injects speed profile or speed offset back onto CAN for Legacy, HW3, or HW4 handlers; those values are now observational unless a plugin explicitly modifies those frames
- Dashboard plugin toggles now apply immediately and batch their persistence, avoiding repeated Wi-Fi stalls while enabling or disabling multiple plugins

## [2.3.2-beta.1] - 2026-04-18

### Changed
- Dashboard builds now ignore all `BEHAVIOUR OPTIONS` from `platformio_profile.h`; these overrides are plugin-managed and are no longer compiled into firmware when `ESP32_DASHBOARD` is enabled

### Fixed
- Dashboard injection stop now also blocks plugin-based frame injection instead of letting enabled plugins keep sending
- TWAI dashboard profile syncing no longer forces commented-out behavior options into the build
- Dashboard boot/runtime state no longer reports legacy built-in ISA, emergency vehicle detection, TLSSC bypass, or nag handling as active in plugin-managed builds
- Plugin cards no longer show the obsolete built-in conflict warning badge and message

## [2.3.1] - 2026-04-18

### Fixed
- Dashboard hardware defaults now follow `platformio_profile.h` reliably for dashboard builds, even when older `DASH_DEFAULT_HW` values still exist in the selected PlatformIO environment
- Reflashing a dashboard build with a new default hardware mode now migrates stale stored hardware defaults from NVS without overwriting an explicit hardware choice made later in the web UI

## [2.3.0] - 2026-04-18

### Added
- Dedicated documentation pages for build and flash setup, dashboard usage, and a docs index for GitHub Pages
- Hardware-specific example plugins for HW3 and HW4 feature replacements, including AD activation, TLSSC bypass, nag suppression, Summon unlock, ISA chime suppression, emergency vehicle detection, and HW4 speed offsets

### Changed
- Dashboard Features card now only exposes Enable Logging; the other vehicle overrides are no longer shown there
- GitHub Actions are now split into separate workflows for tests, releases, and GitHub Pages deployment
- Dashboard and README documentation now reflect the plugin-based override flow and current Pages structure

## [2.2.0] - 2026-04-18

First stable release of the 2.2 series. Bundles all changes from 2.2.0-beta.1 through 2.2.0-beta.14.

### Added
- Plugin Editor UI with live JSON preview, duplicate-name detection, download/export, install support, loading of installed plugins, and a rule test tool for sending generated CAN frames
- CAN Sniffer support for switching between on-wire 11-bit IDs and prefixed DBC JSON IDs, with filtering for both formats
- Dashboard improvements including plugin capacity visibility, default CAN pin hints, a hidden SSID option for the WiFi hotspot, and an Atom S3 Mini injection toggle on the built-in button

### Changed
- Dashboard defaults now follow the selected build flags, including injection-on-boot behavior and vehicle-aware web UI defaults for TWAI and MCP2515 targets
- `esp32_feather_v2_mcp2515` now uses the new MCP2515 driver and web UI
- Reinstalling an existing plugin now preserves its enabled state and works cleanly when the plugin list is already full

### Fixed
- Plugin enable/disable and remove state is now persisted correctly across reboot, and install/remove/toggle actions refresh the dashboard immediately without falling back to a misleading connection error
- Dashboard polling, confirmation flows, and plugin detail panels now behave reliably across reconnects and Chrome on iOS
- WiFi STA/AP persistence, optional NVS reads, injection persistence, and runtime AD gating now behave consistently across reboots and firmware updates
- Atom and Atom S3 builds now use the correct RGB LED pins and a release-safe ESP32 LED API path across the supported CI toolchains

## [2.2.0-beta.14] - 2026-04-18

### Fixed
- Plugin install, remove and enable/disable actions in the dashboard now refresh the plugin list immediately instead of waiting for a manual page refresh
- Dashboard plugin installs no longer fall back to a misleading "Connection error" when the plugin was already applied and only the response body was interrupted
- Atom and Atom S3 dashboard builds now use an ESP32 RGB LED API path that stays compatible across the release CI toolchains

## [2.2.0-beta.13] - 2026-04-18

### Added
- Atom S3 Mini builds can now toggle injection with the built-in button on GPIO41, with the state saved so it persists across reboot

### Fixed
- Atom and Atom S3 dashboard builds now drive the built-in RGB status LED from the correct board pins so injection-off shows red and injection-on shows green reliably

## [2.2.0-beta.12] - 2026-04-17

### Fixed
- Plugin detail panels in the dashboard now stay open across the background plugin-list refresh instead of collapsing unexpectedly
- Dashboard confirmation prompts now use an in-page modal so reboot and other confirm actions work reliably in Chrome on iOS

## [2.2.0-beta.11] - 2026-04-17

### Added
- Plugin Editor can now load installed plugins for in-place editing
- Plugin Editor now includes a rule test tool that can send a generated CAN frame multiple times at a chosen interval

### Changed
- Reinstalling an existing plugin by name now preserves its enabled/disabled state and still works when the plugin list is already full

### Fixed
- Atom S3 and Atom Lite dashboard builds can now save CAN pin settings that use GPIO 6-11 instead of being blocked by the generic ESP32 flash-pin restriction

## [2.2.0-beta.10] - 2026-04-17

### Fixed
- Plugin enabled/disabled state is now persisted across reboot instead of defaulting back to enabled on startup
- Removing a plugin now also clears its persisted enabled/disabled state
- Dashboard background polling now stops cleanly after repeated connection failures and points users to the STA IP after WiFi handoff instead of continuously spamming timeout errors

## [2.2.0-beta.9] - 2026-04-17

### Added
- CAN Sniffer now has a toggle to switch between on-wire 11-bit CAN IDs and DBC JSON IDs with the current bus prefix
- The sniffer filter now accepts both on-wire IDs and prefixed DBC JSON IDs

### Changed
- Migrated `esp32_feather_v2_mcp2515` to the new MCP2515 driver and web UI
- Dashboard feature and injection defaults now follow the selected build flags, and `DASH_INJECTION_ON_BOOT` can be used to start injecting automatically after boot
- Dashboard grid inputs now size correctly in narrower layouts, and `platformio_profile.h` no longer ships with `DRIVER_TWAI` and `HW3` preselected by default

### Fixed
- "Stop Injecting" now persists across reboot instead of silently re-enabling injection on startup
- Runtime AD gating is now applied consistently across Legacy, HW3, and HW4 handlers so blocked mux paths no longer keep injecting

## [2.2.0-beta.8] - 2026-04-16

### Added
- Plugins card now shows the maximum plugin capacity, current usage, and a clearer message when the plugin limit has been reached
- `/plugins` now returns `maxPlugins`, and plugin install errors include the configured maximum

### Fixed
- `DRIVER_TWAI` dashboard builds now treat the vehicle selection in `platformio_profile.h` as the default web UI hardware only; if none is selected, `HW3` is used by default
- WiFi STA credentials are now loaded correctly from NVS on boot after being saved through the dashboard
- Optional AP and WiFi preference reads no longer spam `Preferences` `NOT_FOUND` errors when keys have not been stored yet
- Added the option to show default can pins in webdashboard

## [2.2.0-beta.7] - 2026-04-15

### Added
- Hidden SSID option in the WiFi Hotspot card. When enabled, the access point does not broadcast its SSID — clients have to enter the name manually. Setting is persisted in NVS and included in the settings backup/restore JSON (`ap.hidden`)
- `/ap_config` endpoint accepts a new `hidden` parameter; `/ap_status` returns the current `hidden` flag

## [2.2.0-beta.6] - 2026-04-15

### Changed
- Dashboard layout: the two separate Firmware Update cards (GitHub OTA and manual .bin upload) have been merged into a single card. Manual .bin upload is now a collapsible section under the primary update controls
- WiFi Internet moved into its own top-level card (previously nested inside the Plugins card) — it is used for both firmware updates and plugin downloads, so it deserves its own slot

## [2.2.0-beta.5] - 2026-04-15

### Fixed
- Firmware Update check: no longer offers older versions as "updates". A proper semantic-version comparison is now used (major.minor.patch plus alpha/beta/rc pre-release ranking), so a device on `2.2.0-beta.4` will not be prompted to "update" to `2.0.0` or `2.1.0`
- Auto-Update on Boot uses the same comparison (older releases are skipped)
- CI release job: firmware binaries are now reliably attached to GitHub releases. The workflow first creates the release as a draft, uploads all assets, and then publishes it, which works around the repository's "immutable releases" setting that previously blocked asset uploads after publish

## [2.2.0-beta.4] - 2026-04-15

### Changed
- Default dashboard credentials (`changeme`) are now allowed at build time. Users are expected to change the WiFi AP password and OTA credentials at runtime via the dashboard WiFi Hotspot card (persisted in NVS, OTA-safe)
- Build no longer fails when `DASH_PASS` / `DASH_OTA_PASS` are left at the default `changeme` placeholder

### Removed
- Nag Killer toggle removed from the dashboard Features card. The underlying `NAG_KILLER` build flag remains available for advanced users who want to compile it in

## [2.2.0-beta.3] - 2026-04-15

### Fixed
- Firmware Update check: "JSON parse error" when Beta Channel is enabled (reduced GitHub API response size by using `per_page=1` instead of `per_page=5`, avoids ArduinoJson heap overflow on ESP32)
- CI: firmware artifacts are now correctly attached to GitHub releases. Previous releases failed to attach binaries due to an "immutable release" error when the release was published before the upload step ran

### Changed
- CI release notes: workflow now extracts the matching section from `CHANGELOG.md` for each tag instead of using the whole file, and auto-detects prerelease based on the tag name (`beta`/`alpha`/`rc`)

## [2.2.0-beta.2] - 2026-04-15

### Added
- Auto-Update on Boot: optional toggle in the Firmware Update card. When enabled, the device checks GitHub for a newer release ~15 seconds after the WiFi Internet connection comes up and installs it automatically
- Respects the Beta Channel toggle (only installs prereleases when that is on)
- Setting persisted in NVS so it survives firmware updates
- New endpoints: `/auto_update` (GET/POST)

## [2.2.0-beta.1] - 2026-04-15

### Added
- Plugin Editor card: create plugins via a form UI without writing JSON manually
- Support for all plugin ops: set_bit, set_byte, or_byte, and_byte, checksum
- Per-rule configuration of CAN ID, optional mux value, and send flag
- Live JSON preview updating as you edit, with collapsible rule sections
- Client-side validation (ID, mux, bit 0-63, byte 0-7, value 0-255, hex input `0xFF` supported)
- One-click Install via existing `/plugin_upload` endpoint (no backend changes)
- Download generated plugin as a standalone `.json` file for sharing or backup
- Duplicate-name detection against existing installed plugins

## [2.1.0] - 2026-04-15

First stable release of the 2.1 series. Bundles all changes from 2.1.0-beta.1 through 2.1.0-beta.5.

### Added
- **Rebrand & UX:** renamed UI from "ADUnlock" to "ev-open-can-tools"; dynamic footer with firmware version and device IP; GitHub and Discord links in the footer
- **Plugins:** info icon next to "Plugins" with inline explanation and link to plugin documentation with examples
- **CAN Pins:** runtime-configurable CAN TX/RX GPIO pins via the dashboard, persisted in NVS so custom pin configurations survive OTA firmware updates; validation (GPIO 0-39, TX != RX, GPIO 6-11 blocked for SPI flash)
- **WiFi Internet:** network scanner with RSSI and channel info; static IP configuration (IP, gateway, subnet, DNS); dedicated status and scan endpoints
- **WiFi Hotspot:** change AP name and password via the dashboard; credentials stored in NVS and survive firmware updates
- **OTA firmware updates from GitHub releases:** check for updates and install directly from the dashboard, with beta channel toggle
- **Status badges** ("saved" / "firmware default") on WiFi Hotspot and WiFi Internet cards, plus an info icon explaining NVS persistence
- **Settings Backup / Restore:** export all persistent settings (AP, WiFi Internet, CAN pins, beta flag) as JSON and restore in one go — disaster recovery for full-erase or cross-device migration

### Note
- WiFi credentials have been OTA-safe since the original 2.1 series; the badges and backup feature make that explicit and add recovery paths for full-flash-erase scenarios.

## [2.1.0-beta.5] - 2026-04-15

### Added
- Visible "saved" / "firmware default" status badge on the WiFi Hotspot and WiFi Internet cards
- Info icon on WiFi Hotspot card explaining that credentials are stored in NVS and survive firmware updates
- Settings Backup card: export all persistent settings (AP, WiFi Internet, CAN pins, beta flag) as JSON for safekeeping or migration to another device
- Settings Restore: upload a previously exported JSON to restore all persistent settings in one go
- New endpoints: /settings_export (GET), /settings_import (POST)

### Note
- WiFi credentials were already OTA-safe in prior versions; these changes make the persistence explicit in the UI and add disaster-recovery via backup file

## [2.1.0-beta.4] - 2026-04-15

### Added
- Runtime-configurable CAN TX/RX pins: configure GPIO pins for the TWAI transceiver directly from the dashboard
- Pin configuration is persisted in NVS so it survives OTA firmware updates
- New "CAN Pins" dashboard card with validation (GPIO 0-39, TX != RX, GPIO 6-11 blocked for SPI flash) and reboot flow
- New endpoints: /can_pins (GET/POST)

### Fixed
- OTA updates no longer risk breaking CAN communication on boards with custom pin configurations — once pins are configured via the dashboard they persist across updates

## [2.1.0-beta.3] - 2026-04-15

### Added
- Plugin info icon: click the (i) next to "Plugins" to see an inline explanation of what plugins are and how they work, with a link to the documentation and examples
- Footer community links: GitHub repository and Discord invite are now linked in the dashboard footer

## [2.1.0-beta.2] - 2026-04-15

### Changed
- Rebrand: renamed all UI references from "ADUnlock" to "ev-open-can-tools"
- Footer now shows current firmware version and dynamically adapts to the device IP
- Removed hardcoded hardware references from footer

### Added
- WiFi network scanner: scan and display available networks in the dashboard, select by clicking
- Signal strength indicators (RSSI) and channel info for each scanned network
- Static IP configuration: optionally set IP, gateway, subnet mask and DNS server
- OTA firmware update from GitHub releases: check for updates and install directly from the dashboard
- Beta channel toggle: switch between stable and pre-release firmware versions
- Firmware version auto-injected from VERSION file at build time
- Dedicated WiFi status endpoint (/wifi_status) and scan endpoint (/wifi_scan)
- WiFi hotspot configuration: change AP name and password via the dashboard
- New update endpoints: /update_check, /update_install, /update_beta
- New AP endpoints: /ap_config, /ap_status

## [2.0.0] - 2026-04-14

### Added
- Plugin system: install CAN frame modification rules as JSON files via web dashboard
- Plugin Manager UI card with install from URL, file upload, enable/disable and remove
- Paste JSON (offline): install plugins by pasting JSON directly into the dashboard — no internet or file picker needed
- Plugin detail view: expandable rule inspector showing CAN IDs, mux values and all operations per plugin
- Conflict detection: warns with a visual indicator when plugin CAN IDs overlap with base firmware handlers
- WiFi STA mode (AP+STA) for internet access to download plugins
- Plugin engine with mux-aware matching and operations: set_bit, set_byte, or_byte, and_byte, checksum
- Automatic CAN filter merging for plugin-required IDs
- ArduinoJson v7 dependency for all ESP32 dashboard environments
- New API endpoints: /plugins, /plugin_upload, /plugin_install, /plugin_toggle, /plugin_remove, /wifi_config
- Plugin documentation with format reference, examples and CAN ID table (docs/plugins.md)

### Fixed
- Credential placeholder check in build script now matches platformio_profile.h defaults
- CI release job: firmware artifacts renamed to unique filenames to prevent upload conflicts

## [1.0.0] - 2026-04-10

First release
