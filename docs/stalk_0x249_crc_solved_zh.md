# 0x249 SCCMLeftStalk CRC 破解结果(2023.11 Model Y HW3)

来源:`can_recording (45).csv`(手动模式,超车灯 PULL + 远光 PUSH),bus2 / X197 9/10。

## 帧结构(0x249,4 字节,~20Hz / 50ms)

| 字节 | 含义 |
|------|------|
| b0 | CRC(见下) |
| b1 | 低半字节 = counter(0–15 滚动);bit4-5 = 状态:**1=PULL(超车闪)**,**2=PUSH(远光切换)** |
| b2 | 转向拨杆(本次全为 0) |
| b3 | 0 |

## CRC 公式(已验证 846/846)

```
b0 = BASE[counter] XOR OFFSET[status]
```

- `counter = b1 & 0x0F`,`status = b1 >> 4`
- **BASE[0..15]**(counter 表):
  `9B E8 2A D3 D3 83 4C 5E 3F 5E E2 28 3A 13 AF CE`
- **OFFSET**:status0=0x00,**status1(超车闪)=0x76**,**status2(远光)=0xEC**,status4=0xF7

> ⚠️ 仅在 b2=b3=0(转向拨杆 idle)时成立。若需带转向信号的帧,要另抓。
> 该表为 CRC 算法的固定属性,理论上不随上电变化,但建议重启后再抓一次确认。

## 现成 CRC 查表(b2=b3=0)

**PUSH / 远光(status2)** counter 0→15:
```
77 04 C6 3F 3F 6F A0 B2 D3 B2 0E C4 D6 FF 43 22
```
**PULL / 超车闪(status1)** counter 0→15:
```
ED 9E 5C A5 A5 F5 3A 28 49 28 94 5E 4C 65 D9 B8
```

## C 实现片段

```c
static const uint8_t kStalkBase[16] = {
    0x9B,0xE8,0x2A,0xD3,0xD3,0x83,0x4C,0x5E,
    0x3F,0x5E,0xE2,0x28,0x3A,0x13,0xAF,0xCE};
// status: 1=PULL(flash), 2=PUSH(high beam)
static inline uint8_t stalkCrc(uint8_t counter, uint8_t status) {
    static const uint8_t off[5] = {0x00,0x76,0xEC,0x00,0xF7};
    return kStalkBase[counter & 0x0F] ^ off[status & 0x07];
}
// 组帧: b0=stalkCrc(cnt,2); b1=(2<<4)|cnt; b2=0; b3=0;  // 远光 PUSH
```

## 关键结论

1. **bus2(9/10)= 车身/灯光总线**,只有 0x249(拨杆)+ 0x3F5(灯态反馈),**没有 0x3E9(DAS)**。
   → 本总线上控制远光的唯一手段就是拨杆 0x249 → **路径 A(改 0x3E9)在此总线不可行,只能走路径 B**。
2. CRC 已解,可为任意 counter 生成合法的 PUSH/PULL 帧。
3. 0x3F5 灯态在 PUSH 事件时同步变化 → 可做闭环确认远光真亮。

## 仍需验证(下一步)

- 本数据是**手动模式**,**未含 FSD**。仍需按抓帧文档第 5 段:**FSD 行驶中推拨杆,确认远光能否常亮**。
- **双发送端冲突**:真实 SCCM 一直在发 0x249,直接注入会与它仲裁冲突 → 大概率需要 **inline MITM**(剪线,两控制器跨接 9/10),或实测注入能否"压过"。
- 重启后复测 CRC 表是否不变。
