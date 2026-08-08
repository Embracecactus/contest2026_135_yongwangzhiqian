# BK7258 官方 normal bootloader：Ghidra 静态证据

日期：2026-08-07
状态：部分深度静态逆向；未烧录，未修改 SDK/NuttX

## 1. 分析对象

```text
cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin
size    = 52,352 B
SHA-256 = 105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6
```

它按 Cortex-M33 Thumb、基址 `0x02000000` 导入 Ghidra。首部可直接读取为：

```text
vector[0] = 0x28030000
vector[1] = 0x020001c1
offset 0x100 = "BK7236\x10\0"
offset 0x118 = "bc31115"
```

与公开 A/B bootloader 相比，前两个向量字和 `0x100` 的 `BK7236` raw 头一致；版本
字符串不同（A/B 是 `162e531`）。因此可以确认它们使用同一种由更早阶段接受的 raw
启动封装。这个结论不包含、也不能推出 Secure-Boot Manifest 格式。

## 2. 从 Reset Handler 重建的直接启动路径

raw binary 默认不会把 Cortex-M 向量表当作分析入口。重新从 reset vector
`0x020001c0` 种入入口后，Ghidra 在本次导入中识别出 96 个函数；这解决了早先只
识别少数 Flash helper、却遗漏主启动路径的问题。函数数只是当前导入结果，不是
“已完整还原 52 KiB”的证明。

normal boot 的可直接跟踪 Reset 路径是：

```text
0x020001c0 Reset_Handler
  -> 0x02000148 early SoC state
  -> 0x0200102a WDT initialisation
  -> 0x020003b0 UART1 setup
  -> 0x0200073c clock setup
  -> MSPLIM = 0x2802f800
  -> cache/runtime-init tables
  -> 0x02001720(0x02010000)
       -> VTOR = 0x02010000
       -> MSP = *(uint32_t *)0x02010000
       -> clear general registers; branch to vector[1]
```

`0x020001c0 + 0x54` contains the little-endian literal `0x2802f800`; therefore
`0x28030000` is the initial MSP from the vector table, **not** MSPLIM. The same
literal and instruction sequence occur in the official A/B bootloader.

This is not merely similar pseudocode: four early helper bodies are
byte-identical between the exact normal and A/B binaries after their different
link addresses are accounted for:

| Shared behavior | normal address | A/B address | compared body |
|---|---:|---:|---:|
| early SoC preparation | `0x02000148` | `0x02000148` | 88 bytes |
| WDT setup | `0x02000fe4` | `0x02000fe8` | 44 bytes |
| UART1 setup | `0x020003b0` | `0x02000358` | 100 bytes |
| Flash/cache setup | `0x02000280` | `0x02000228` | 80 bytes |

Thus the A/B selector adds policy on top of the same public early-start ABI;
it is not a separate Secure-Boot stage.

The clock initialisers are not byte-identical (`normal: 0x0200073c`, `A/B:
0x020006e4`), but their independently decompiled control flow has the same
early register preparation and the same input-selector cases before different
tail-side reporting. This is semantic shared-start evidence, not a claim of an
identical byte range.

This also corrects an older inference: `0x02001444` contains a FAL/download
entry which calls `0x020017d0`, but the direct Reset call graph above does not
call it. The binary contains FOTA/RBL helper code, yet normal cold reset itself
performs a fixed handoff to the logical CP base `0x02010000`; it does not first
enter a recovered dynamic FAL partition-selection state machine.

## 3. 独立 FAL/download 入口的已恢复范围

把 `0x020013ec`、`0x02001444` 和它们的直接子函数另行种入后，Ghidra 当前列出 140
个函数。这个入口没有来自 Reset Handler 的直接代码引用，因此只能称为 **独立的
FAL/download 入口**；它的外部触发方式仍未知。进一步的入站引用查询显示
`0x020013ec` 在这份 raw binary 内没有任何已识别 code/data 引用，并且整个 raw
文件也没有其 Thumb 地址 `0x020013ed` 的 little-endian 32-bit literal。这个负结果只排除了本 binary 内
的直接入口来源；它不能区分 BootROM 特殊模式、调试器或其他外部组件。

其已证实的调用关系如下：

```text
0x020013ec
  -> device/FAL initialisation
  -> 0x02001444
       -> 0x02001460  (download result dispatcher)
            -> 0x02003580 (find "download" partition)
                 -> validate diff-FOTA header and CRC32
                 -> verify RBL body CRC32
                 -> erase destination
                 -> process payload in 16 KiB chunks
                 -> verify destination hash
       -> 0x020017d0  (find normal app partition)
            -> 0x0200176c (UART/cache cleanup and VTOR/MSP handoff)
```

`0x02003580` directly looks up the `download` partition. Its diff-FOTA header
parser reads exactly `0x44` bytes: its first four bytes must be ASCII `bkbl`,
then it verifies a header CRC32 and a body CRC32, and rejects a compression
parameter other than `0x48000`. `FOTAL` is a distinct resume-journal marker,
not this input header's magic. The entry also maintains short progress records
while consuming payload blocks. `0x0200210c` uses 16 KiB input chunks and
contains the source string
`aes_decrypt_inflate_handler`; this demonstrates a payload decode path, but it
does **not** demonstrate publisher authentication or a Secure-Boot signature.

The result dispatcher has two relevant observable outcomes:

- no usable download / selected nonfatal parse outcomes: print a diagnostic and
  continue to the normal-partition handoff;
- completed download or a fatal integrity/decode failure: print a diagnostic
  and request the watchdog reset path.

This is a proprietary differential-FOTA protocol with RBL/CRC/hash checks. It
is neither MCUboot nor a BL1 Manifest protocol and must not be copied into the
project merely to obtain A/B or signed-image behavior.

### 3.1 已定界的 diff-FOTA 头和恢复记录

`FUN_02002970(download_base, download_length)` first reads `0x44` logical bytes
from the `download` partition. The following offsets and checks come directly
from the decompilation; names marked “用途未命名” deliberately do not invent SDK
field names.

| Offset | Size | Direct code evidence |
|---:|---:|---|
| `0x00` | 4 | must equal ASCII `bkbl`; otherwise prints `not diff-fota header!` |
| `0x04` | 4 | **preimage byte count** (role inferred from data flow): bounds reads from the existing target partition while the diff decoder issues copy-from-old-image operations |
| `0x08` | 4 | **preimage CRC32** (role inferred from data flow): compared with CRC32 of exactly `+0x04` bytes from that existing partition; mismatch prints `file version not match!` and prevents application |
| `0x0c` | 4 | not read by the recovered direct path; reserved/other-mode field, semantic unknown |
| `0x10` | 4 | postimage/new-file byte count: checked against target capacity and final produced size |
| `0x1c` | 4 | stored diff payload byte count |
| `0x20` | 4 | CRC32 expected for bytes `[0x44, 0x44 + payload_size)` |
| `0x24` | 4 | must be `0x48000`; its rounded-up 4 KiB page count sizes working state and the decoder reads the same field to move its rolling page window. The vendor field name remains unknown. |
| `0x2c` | 4 | cleared to zero before return; exact persistent-state name not recovered |
| `0x40` | 4 | CRC32 expected for the first `0x40` header bytes |

The literal `FOTAL\0` occurs separately at binary address `0x02002cac`. The
code uses an eight-byte per-update resume record whose first six bytes are that
marker and whose final two bytes are a phase word. A nonblank accepted record
has phase `0xf0f0`; `FUN_02001b20` writes a new journal copy first with
`0xfcfc`, commits it as `0xf0f0`, marks the old copy `0xc0c0`, then erases the
old page. Therefore `0xf0f0` is the only directly accepted stable state;
`0xfcfc` and `0xc0c0` are observable interrupted/retired rotation states. The
record-page entries following it are four bytes each: the first two and third
byte are indices/parameters whose exact names are not recovered, while byte
`+3` is explicitly written and recognized as this phase sequence:

```text
0xfc  ->  0xf0  ->  0xc0  ->  0x00
prepare    valid     updating   done
```

The English words above are only the corresponding diagnostic labels in the
binary (`prep`, `valid`, `updating`, `done`), not a claim about an official
public struct. This is a normal-FAL internal restart journal. It is separate
from the A/B `ota_fina_executive` state sector and must not be used to infer the
A/B boot policy.

## 4. 反编译得到的直接代码证据

### 4.1 32+2 CRC 编码写入

`FUN_02001566`（地址 `0x02001566`）的参数约束和循环直接表明：

1. 输入长度必须按 32 字节对齐；
2. 逻辑 XIP 目标为 `0x02000000 + (logical / 32) * 34`；
3. 每个 32 字节块计算 CRC16，使用多项式 `0x8005`；
4. 写出的物理块是 32 数据字节加 2 CRC 字节；
5. 每 0x1100 字节缓冲区落盘一次。

这与 A/B binary 已恢复的 `0x02001e0a`、SDK `beken_onchip_crc` 和项目的 32+2
地址域完全同类。它是 **Flash 物理编码**，不是签名或认证。

### 4.2 擦除与编程原语

- `FUN_0200169c`（`0x0200169c`）按 64 KiB 和 4 KiB 两种粒度擦除 Flash；其 XIP
  目标为 `0x02000000 + (offset & mask)`。
- `FUN_02000f34`（`0x02000f34`）将任意长度输入填充为 32 字节页后写入 Flash 控制器。
- `FUN_02000ec4`（`0x02000ec4`）从 Flash 控制器一次取回 32 字节逻辑数据。

这些函数证明 normal binary 内建下载/FOTA 所需的读、擦、写及 CRC 物理展开能力。

## 5. 字符串能证明什么，不能证明什么

binary 中有下列字符串：

```text
u_bootloader enter
dl part NO_OTA_DATA!
dl part NO_RBL_HEAD or no app1!
img hash err!
beken_onchip_crc
Verify firmware hash(calc.hash: ...)
RBL / FOTAL / FOTA*
fal_partition_find / fal_partition_read
```

它们与 normal/FOTA/RBL 数据面一致。结合 Reset call graph，现在能更准确地说：这些
代码是 binary 的功能集合，但不能因为它们存在就宣称它们属于 cold-reset 默认路径。
FOTA/download entry 的外部触发源、完整协议和数据结构仍未恢复。

## 6. 与公开 A/B bootloader 的关系

| 项目 | normal bootloader | A/B bootloader |
|---|---|---|
| raw 头 / 初始向量 | 相同封装形式 | 相同封装形式 |
| 32+2 CRC | 已从读写函数直接确认 | 已从编码函数直接确认 |
| RBL/FNV/FOTA | helper 已确认；不在默认 Reset 路径 | 已恢复 RBL/FNV 及 A/B 状态机 |
| 默认 reset 选择 | 固定跳至 `0x02010000` | 读取状态并选择 `appa` / `s_app` |
| `appa` / `s_app` remap | 不适用/未见 | 已直接恢复 |
| `ota_fina_executive` trial/confirm | 不在默认 Reset 路径 | 已直接恢复 |
| MCUboot / Manifest / EC256 签名验证 | 未发现直接证据 | 未发现直接证据 |

结论：normal 与 A/B 是同一公开 non-secure 生态下的两种 bootloader，而不是一对
BL1/BL2。A/B 是槽选择 bootloader；normal 包含 FOTA/下载数据面。两者都不能充当
缺失的 BK7258 Secure BL1 或官方 MCUboot BL2 的替代证据。

## 7. 下一步与边界

已完成 FAL 输入的 `bkbl` 头字段边界、`FOTAL` 恢复记录、preimage/postimage 数据流及
RBL 头的基本边界。v3.1.1.9 公开源码没有 `bkbl/FOTAL` 生成端；所见的另一份公开
BK7258 normal binary 也不带这套字符串。BK7236 的公开 BootROM reference 虽有
Secure-Boot/boot-mode 代码，却是不同芯片的 ROM 映像，不能用来证明或猜测 BK7258
`0x020013ec` 的进入方式。因此 FAL/download 的外部触发已到达当前公开工件的硬边界，
不是继续反编译就能补出的字段。

如取得 BK7258 BootROM、可复现的特殊启动模式寄存器/串口记录或 FAL 生成工具，才可
重新打开该问题；否则不把它们误当成 normal cold-reset 的选择逻辑。final VTOR/MSP
handoff 已由 `0x02001720` 直接确认。
在取得 Secure-Boot binary 或厂家规范以前：

- 不猜测 Manifest 二进制；
- 不把 SDK 通用模板升级为 BK7258 事实；
- 不写 OTP/eFuse；
- 不把项目 board-owned BL1/BL2 原型称为官方逆向结果。

相关记录：

- [A/B bootloader Ghidra 逆向](2026-08-07-ghidra-bk7258-ab-bootloader.md)
- [启动链证据矩阵](2026-08-07-bk7258-boot-chain-evidence-matrix.md)
