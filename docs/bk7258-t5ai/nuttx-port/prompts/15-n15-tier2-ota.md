# BK7258 Stage N15：成对 CP/AP OTA、回退与掉电恢复

> 日期：2026-08-03
> 状态：**IN PROGRESS / N15-M 布局迁移已 board-verified，完整 OTA 尚未实现**
> SDK：只使用 official Beken v3.1.1.9
> 架构决策：[ADR-004](../../../../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md)
> 历史被否决方案：[ADR-003](../../../../memory/decisions/ADR-003-n15-paired-sector-swap.md)

## 1. 当前结论

项目已从 N14 的 CP/LittleFS/AP 分散布局迁移到与 official v3.1.1.9
`app_ab` 一致的连续 CP/AP A/B 几何。迁移由 team-owned linker、bootloader、packer、
loader preflight 和 verifier 完成；official NuttX、apps、SDK source 和 SDK static
libraries 没有修改。

N15-M 已完成一次经 owner 明确授权的破坏性迁移。旧 LittleFS 内容已清空，新 LittleFS
已在 `0x600000..0x700000` 自动格式化、挂载并通过重启持久化探针。当前 bootloader 仍只启动
A；B 中只是与 A 同 generation 的 seed，manifest 明确为
`boot_selectable=false`、`rbl_header_present=false`。因此：

- 新布局基线已经 `board-verified`；
- 成对 OTA、candidate staging、trial、confirm、rollback 和掉电恢复仍未实现；
- `writes_enabled=false` 现在表示 runtime OTA writer/slot selection 仍关闭，不否定已经完成的
  一次性外部 loader 迁移。

## 2. 已冻结布局

| 区域 | raw physical range | 大小 | 当前策略 |
|---|---:|---:|---|
| bootloader | `0x000000..0x011000` | `0x011000` | team source，official envelope |
| CP A | `0x011000..0x165000` | `0x154000` | primary active |
| AP A | `0x165000..0x286000` | `0x121000` | primary active |
| CP/AP B (`s_app`) | `0x286000..0x4fb000` | `0x275000` | same-pair seed，当前不可选择 |
| trial metadata | `0x4fb000..0x4fc000` | `0x001000` | 当前为 erased/unarmed |
| vendor `usr_config` | `0x4fc000..0x50a000` | `0x00e000` | loader migration 不覆盖 |
| reserved | `0x50a000..0x600000` | `0x0f6000` | 不覆盖 |
| CP LittleFS | `0x600000..0x700000` | `0x100000` | 已清空并重新建立 |
| reserved | `0x700000..0x7fa000` | `0x0fa000` | 不覆盖 |
| official tail | `0x7fa000..0x800000` | `0x006000` | loader/OTA 禁止覆盖 |

XIP 地址：

- CP：`0x02010000..0x02150000`；
- AP：`0x02150000..0x02260000`；
- AP CPU0/CPU1 vectors：`0x02150000` / `0x02150200`。

迁移不是 chip erase。loader 只接收两段：

```text
all-app-factory.bin@0x000000-0x4fc000
littlefs_factory_clear.bin@0x600000-0x100000
```

把它做成两段而不是一个稠密 `0x700000` 文件，是为了让 `usr_config` 与两个 reserved
区间根本不出现在写集合中。

## 3. 主机门禁

`bk7258_ab_layout.py` 是 canonical host model；
`verify_bk7258_ota_layout.py` 对照 exact v3.1.1.9 official CSV，并交叉检查 team header、
linker、postbuild、boot FAL、MTD、packer、debug SOP 与 factory verifier。

`verify_bk7258_factory_layout.py` 逐字节验证：

- A 中 boot/CP/AP 文件、padding 与物理上界；
- B 是 A 的 byte-exact pair copy，且仍不可启动；
- metadata 为 `0xff`；
- LittleFS clear image 精确为 1 MiB `0xff`；
- write ranges 精确为两段，排除 `usr_config`、reserved 与 calibration tail；
- manifest 中所有长度、offset 与 SHA-256 匹配实际文件。

exact v3.1.1.9 `cp_nsh_psram + ap_smp_psram` clean build 已通过：SDK CP/AP checksum、
bootloader、RPTUN layout、BLE GATT、PSRAM 与 factory byte verifier 全部 PASS。
最终收口还证明两个负例会 fail-closed：截短 LittleFS clear image 因 SHA-256 drift 被拒绝，
错误/旧 layout ID 因 layout drift 被拒绝。

迁移产物：

| artifact | length | SHA-256 |
|---|---:|---|
| `all-app-factory.bin` | `0x4fc000` | `4722e2a81504e5e321f67850c518b0b919b79e796214481d1e0dd01bf9cf8e4b` |
| `littlefs_factory_clear.bin` | `0x100000` | `f5fb04aa5b882706b9309e885f19477261336ef76a150c3b4d3489dfac3953ec` |
| `s_app_seed.bin` | `0x275000` | `aa89797aa90bca393061ed100ca03c5cbed7194fb592cc86f60a7fe8af263d4d` |

## 4. N15-M 板端证据

2026-08-03 22:26 通过 COM7/BKFIL 完成两段写入，两段均
`EraseFlash -> pass`、`WriteFlash -> pass`、`Writing Flash OK`。UART 到
`PASS_NSH`。随后验证：

- AP `READY/error=0`，VTOR `0x02150000`；
- CPU2 `SCHEDULER_ONLINE/error=0`，vector `0x02150200`；
- RPTUN `CONNECTED`，supervisor `HEALTHY`；
- LittleFS 首次 autoformat 后 `BK7258LFS-OK`，3 次 physical reset 后仍可读取；
- PSRAM info + heap 256/256 PASS，AP CPU0/CPU1 16/16，free 稳定；
- SDK timer 256/256 PASS；
- RPMsg 六场景×20 PASS，heap 稳定；
- RPMsgFS 四档×1 PASS；
- Bluetooth info PASS，BD_ADDR `c8:47:8c:47:47:48`、fallback 0；
- COM7 RTS physical reset 3/3 均 `cold_path=yes`、`PASS_NSH`。

迁移前完整 8 MiB 曾以 6 Mbps 读取，但重复分析证明 BKFIL 高速 read 会偶发插入
128-byte 全零块，所以该 dump 只作 forensic reference，禁止直接回刷。迁移后
`usr_config` 与 official tail 使用 115200 连续两次读取，分别 byte-identical；其 SHA-256 为
`d078e2a2...e79acf` 与 `fa92844a...58c77`。完整证据见
[N15-M verification](../../../../progress/verification/2026-08-03-n15-migration-board-verification.md)。

## 5. 下一阶段

下一阶段是 N15-A，不再改布局：

1. 复刻 exact v3.1.1.9 `s_app` RBL container、96-byte header 与 FNV/CRC validation；
2. 定义 team pair manifest、generation/version policy 与 deterministic pack；
3. 增加 size/hash/layout/version/corruption 负例；
4. 保持 B writer、remap 和 trial metadata mutation 关闭；
5. 评审签名与 anti-rollback。CRC/FNV/SHA 只能称为 integrity，不能称为 authenticity。

后续门禁：

| 子阶段 | 工作 | 退出条件 |
|---|---|---|
| N15-A | pair bundle + exact RBL/parser | host positive/negative tests PASS；无板写 |
| N15-B | CP-only `s_app` staging wrapper | 有界写/read-back；A/data/tail零触碰 |
| N15-C | team boot remap + pair validation | A/B pair均可验证；坏candidate fail-closed |
| N15-D | one-trial confirm/revert | 未确认自动回退；确认后稳定 |
| N15-E | reset/corruption/power-loss injection | 固定矩阵无mixed generation |
| N15-V | full regression + security boundary | 全部通过后才标完整N15 `board-verified` |

## 6. 不变量

- 只使用 official v3.1.1.9；legacy 仅保留，N15 完成后另行决定是否验证。
- 不修改 official NuttX/apps/SDK source 或 SDK static libraries。
- CP 与 AP 必须是同一 generation；任何 mismatch 都不得 trial boot。
- normal sparse update必须使用新 offset，并保留B、LittleFS、`usr_config`与tail。
- 禁止 chip erase，禁止把历史 ADR-003 journal/scratch 地址重新接回 active build。
- 禁止运行会使 Windows 卡顿的 `BLEDebug.EXE`。
