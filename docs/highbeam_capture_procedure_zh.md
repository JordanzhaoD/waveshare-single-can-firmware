# FSD 手动远光灯 — 上车抓帧测试流程

> 目标:采集真实 CAN 数据,验证 **路径 B(FSD 驾驶状态下进入手动远光、用拨杆开启)** 是否可行;
> 若不可行,再用同一批数据落地 **路径 A(FSD 下一键强制远光常亮)**。
> 同时反推 Tesla 帧的 **滚动计数器 + 校验** 算法,这是写注入/MITM 代码的前提。

适用车辆:**2023 年 11 月生产 Model Y,HW3(AMD 处理器)**
适用硬件:LILYGO T-2CAN,接 X197(pin 13/14 = bus1/TWAI,pin 9/10 = bus2/MCP2515)

---

## ⚠️ 安全须知(务必先读)

1. **行驶中抓数据需要两个人**:一人专心开车,一人操作手机/面板。**驾驶员不要看手机**。
2. 涉及 FSD 的步骤请在 **车流稀少、视野好的道路** 进行,夜间测试远光请避免对向来车时长亮。
3. 远光长亮可能晃到对向车辆,**测试用,点到为止**,不要长时间强制开启。
4. 任何异常(仪表报警、灯光乱闪、FSD 退出)立即 **关闭面板开关、停止注入**,本流程前半段是**纯被动监听,不会发任何帧**,是安全的。
5. 本流程**第 1~6 段只读不写**(只录制,不注入),对车辆无影响。真正的注入测试在数据分析之后才做。

---

## 0. 准备工作

- [ ] T-2CAN 已按 X197 接线(13/14 → TWAI,9/10 → MCP2515),已刷入最新固件
- [ ] 车辆上电(踩刹车进 READY 或至少仪表/灯光系统上电)
- [ ] 手机连 WiFi `LILYGO-T-2CAN`,浏览器打开 `http://100.100.1.1/`
- [ ] 面板能正常显示,**Bus2 Sniffer** 卡有数据跳动
- [ ] 准备记录本/手机备忘录,用来记每段录制的"做了什么动作 + 大概第几秒"

---

## 目标信号(三帧)

| 帧 ID | 名称 | 作用 | 抓取重点 |
|-------|------|------|---------|
| `0x249` | SCCMLeftStalk | 左拨杆(远光拉/推) | byte0=CRC8,byte1 低半字节=counter,byte1 bit4-5=高远光状态 |
| `0x3E9` | DAS_bodyControls | 自动驾驶电脑灯光命令 | byte1 bit3-4=远光决定,byte6 高半字节=counter,byte7=checksum |
| `0x3F5` | VCFRONT_lighting | 真实灯态反馈 | byte4 bit0-3=左右远光亮灭(用来确认灯真的响应) |

> 字节布局详表见 **附录 A**。

---

## 1. 【关键】确认信号在哪一路总线(决定整个方案能否成立)

1. 车辆上电,**先不要动任何东西**。
2. 看面板 **Bus2 Sniffer**(X197 9/10)卡里出现的 ID 列表;再看 **CAN ▸ CAN Sniffer**(X197 13/14)里的 ID 列表。
3. 找 `0x249`、`0x3E9`、`0x3F5` 出现在 **哪一路**:
   - ✅ 如果在 **bus2(9/10)** → 符合预期,继续。
   - ⚠️ 如果在 **bus1(13/14)** → 方案需调整(灯光帧在另一路),**先告诉我**。
   - ❌ 如果 **两路都没有** → X197 这两对针脚不带车身总线,需要换抓取点,**停下来找我**。

> **这一步过不了,后面都白做。请先把两路各看到哪些 ID 拍照/记录发我。**

---

## 2. 静止基线(确认 counter/checksum 规律)

> 目的:车不动、灯不动时,看 `0x249`/`0x3E9` 的 counter 怎么递增、checksum 怎么变。

1. 车辆 READY,**不碰拨杆、不开 FSD**。
2. 面板 **Bus2 Sniffer** 卡点 **「Capture lighting IDs (249/3E9/3F5)」** 开始录制。
3. 静止录 **约 30 秒**。
4. 到 **CAN ▸ Recorder** 区点 **Stop**,再点 **Download CSV**。
5. 文件重命名为:**`01_baseline.csv`**

---

## 3. 拨杆手动动作(静止,这是路径 B 的核心素材)

> 目的:抓到"驾驶员真实操作拨杆"时 `0x249` 的字节,作为模拟的模板。
> **每个动作单独录一段**,这样时间对应关系最清晰。

| 段 | 动作 | 录制文件名 |
|----|------|-----------|
| 3a | 开始录 → 等 3 秒 → **向里拉一下拨杆(超车闪灯/PULL)** → 等 3 秒 → 停 | `02_stalk_pull_flash.csv` |
| 3b | 开始录 → 等 3 秒 → **向前推拨杆开远光(PUSH/toggle ON)** → 等 3 秒 → **再推一下关远光(toggle OFF)** → 停 | `03_stalk_push_toggle.csv` |
| 3c | 开始录 → 等 3 秒 → **连续拉 3 下(模拟爆闪节奏)** → 停 | `04_stalk_pull_x3.csv` |

> 操作时**口头报时**或心里记"第几秒做的动作",写在备忘录里,例如:`03: 第4秒推开,第9秒推关`。
> 每段都用 **Capture lighting IDs** 按钮起录、Recorder 区停录并下载。

---

## 4. FSD 下自动远光(路径 A 的素材 + 对照)

> 目的:抓 FSD 激活、AHB 自动开/关远光时 `0x3E9` 的 `DAS_highLowBeamDecision` 怎么跳变。
> **需要行驶,务必两人。**

1. 找一段**夜间/隧道**或光线会变化的路,**激活 FSD**。
2. 副驾点 **Capture lighting IDs** 起录。
3. 让车 **自动开/关远光至少各 2~3 次**(进出隧道、有无对向车),录 **约 60 秒**。
4. 停录、下载,命名:**`05_fsd_autohighbeam.csv`**
5. 备忘录记下:大概第几秒灯亮、第几秒灯灭(对照 `0x3F5` 反馈)。

---

## 5. 【最关键】FSD 下手动推拨杆 —— 验证路径 B 能否成立

> 这一段直接决定你想要的"FSD 下用拨杆开远光常亮"是否可行。

1. **激活 FSD**,保持 AHB 默认(自动)状态。
2. 副驾点 **Capture lighting IDs** 起录。
3. 驾驶员在 FSD 行驶中 **向前推一下拨杆**(尝试手动开远光),保持 **10 秒不动**,观察:
   - 远光是否亮起?
   - 是否 **保持常亮**,还是几秒后被系统自动关掉?
4. 再 **推一下关闭**,停录、下载,命名:**`06_fsd_manual_pushstalk.csv`**
5. 备忘录**重点记**:
   - 推拨杆后远光「亮了吗?」「亮了几秒?」「自己灭了还是一直亮?」
   - 仪表上远光指示灯(蓝色)是否点亮?

> **判定:**
> - 推一下能进入手动并**常亮** → ✅ 路径 B 成立,我按模拟拨杆来做。
> - 亮一下又被 AHB 关掉 / 根本不亮 → ❌ 路径 B 不成立,改用路径 A(覆盖 0x3E9)。

---

## 6. 数据交付

把以下内容发我:

1. 第 1 段:两路 sniffer 看到的 ID 列表(截图或文字)
2. CSV 文件:`01_baseline.csv` ~ `06_fsd_manual_pushstalk.csv`
3. 每段的动作备忘录(第几秒做了什么、灯的反应)

我会用这些数据:
- 反推 `0x249` 的 **CRC8** 和 `0x3E9` 的 **checksum** 算法(用 ≥256 帧拟合)
- 确认远光控制走哪条路径、能否常亮
- 确定是否需要把接线改成 **inline MITM**(剪线,两个控制器跨接同一总线)
- 写出对应的注入/转发代码

---

## 7. 录制小贴士

- 录制器上限 **60 秒 / 8000 帧**,带 ID 过滤后只录这 3 个 ID,足够用。
- **每段录完先下载再开下一段**,否则 `/rec.csv` 会被覆盖。
- CSV 列含义:`ts_ms,dir,bus,id,dlc,b0..b7`
  - `ts_ms` = 开机毫秒数(同一段内可做相对时间)
  - `bus` = `2`(MCP2515 / 9/10)或 `1`(TWAI / 13/14)
  - `dir` = `R` 收 / `T` 发 / `E` 错
- 也可不用按钮、直接浏览器开 `http://100.100.1.1/rec_start?ids=249,3E9,3F5` 起录。

---

## 附录 A:目标帧字节布局(model3dbc,小端 Intel)

### 0x249 SCCMLeftStalk(4 字节,SCCM 模块发送)
| 信号 | 起始位 | 长度 | 字节位置 | 取值 |
|------|-------|------|---------|------|
| SCCM_leftStalkCrc | 0 | 8 | byte0 | CRC8 |
| SCCM_leftStalkCounter | 8 | 4 | byte1 低半字节 | 0–15 滚动 |
| SCCM_highBeamStalkStatus | 12 | 2 | byte1 bit4-5 | 0=IDLE,**1=PULL(爆闪)**,**2=PUSH(远光切换)**,3=SNA |
| SCCM_turnIndicatorStalkStatus | 16 | 4 | byte2 低半字节 | 转向拨杆 |

### 0x3E9 DAS_bodyControls(8 字节,自动驾驶电脑发送)
| 信号 | 起始位 | 长度 | 字节位置 | 取值 |
|------|-------|------|---------|------|
| DAS_headlightRequest | 0 | 2 | byte0 bit0-1 | 0=OFF,1=ON |
| DAS_highLowBeamDecision | 11 | 2 | byte1 bit3-4 | 0=未决,1=远光OFF,**2=远光ON**,3=SNA |
| DAS_highLowBeamOffReason | 15 | 3 | byte1.7+byte2 | 关远光原因 |
| DAS_ahlbOverride | 24 | 1 | byte3 bit0 | 自动远光覆盖标志 |
| DAS_bodyControlsCounter | 52 | 4 | byte6 高半字节 | 0–15 滚动 |
| DAS_bodyControlsChecksum | 56 | 8 | byte7 | 校验 |

### 0x3F5 VCFRONT_lighting(8 字节,只读反馈)
| 信号 | 起始位 | 字节 | 取值 |
|------|-------|------|------|
| VCFRONT_highBeamLeftStatus | 32 | byte4 bit0-1 | 0=OFF,1=ON,2=FAULT |
| VCFRONT_highBeamRightStatus | 34 | byte4 bit2-3 | 同上 |
| VCFRONT_highBeamSwitchActive | 58 | byte7 bit2 | 远光开关激活 |

---

## 附录 B:数据来源

- DBC:[joshwardell/model3dbc](https://github.com/joshwardell/model3dbc)
- 信号浏览:[tcan.latency.is](https://tcan.latency.is/?frame=0x3E9__DAS_bodyControls&plat=ModelY)
