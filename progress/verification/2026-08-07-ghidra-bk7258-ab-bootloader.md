# BK7258 官方 A/B bootloader：Ghidra 逆向证据

日期：2026-08-07  
状态：A/B 选择、交接、失败复位和 120 MHz 时钟交接已完成静态恢复与实板验证；未修改 SDK/NuttX

## 1. 分析对象与可复现命令

本次只使用官方 v3.1.1.9 SDK 的 BK7258 二进制：

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin` | 18,720 B | `3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047` |
| `cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` | 52,352 B | `105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6` |

两个 raw binary 都按 Cortex-M33 Thumb、加载基址 `0x02000000` 导入 Ghidra。A/B
工程的函数/调用边和反编译 C 输出由仓库工具
`tools/ghidra/ExportBk7258Decomp.java` 导出。导出结果保留在临时目录
`/tmp/bk7258-ab-decomp.*`，不是项目输入，也不参与固件构建。

## 2. 已确认的启动链

```text
BootROM
  └─ BK7236 raw bootloader header
       └─ official A/B bootloader
            ├─ 初始化 flash/offset 映射和 WDT
            ├─ 读取 ota_fina_executive 四个状态字节
            ├─ 选择 appa 或 s_app（B）
            ├─ 计算 RBL/FNV hash；失败时尝试另一槽
            ├─ 写入 Flash-controller offset-enable/offset 参数
            └─ 按选中分区的 32+2 CRC 物理地址跳转
```

A/B 二进制中确认存在的字符串包括：`appa`、`s_app`、`ota_fina_executive`、
`beken_onchip_crc`、`Verify firmware hash`、`A switch B`、`B switch A`、
`A valid (not all 0xFF, magic ok)` 和 `B valid (not all 0xFF, magic ok)`。

这说明它是官方的 A/B OTA bootloader，但这些字符串和调用路径中没有发现
MCUboot image magic、MCUboot TLV、EC256/RSA 签名校验或 secure-boot manifest
解析。它不能被当成 BK7258 Secure Boot/MCUboot 的 exact BL1/BL2 实现。

## 3. 关键函数证据

### 3.0 向量表、BootROM 头和启动入口

Ghidra/raw dump 对 A/B binary 的入口证据为：

```text
vector[0] = 0x28030000                 ; boot SRAM 顶部
vector[1] = 0x020001c1                 ; Reset_Handler，Thumb
0x02000100..0x02000107 = BK7236\x10\0x00
0x02000118..            = "162e531"     ; A/B binary 内的版本字符串
```

`Reset_Handler (0x020001c0)` 先调用早期 SoC/Flash 初始化、UART1、系统时钟，设置
`MSPLIM = 0x2802f800`，随后清理/配置 Flash cache，再执行 linker init table。初始
MSP 才是 vector[0] 的 `0x28030000`。`0x02000214` 的原始 little-endian literal 是
`0x2802f800`，因此这不是反编译器命名或注释推断。这个启动入口和 52 KiB normal
bootloader 都是 BootROM 直接加载的 raw bootloader；本次
A/B binary 没有发现一个独立、可由该 binary 再加载的 MCUboot BL2 image。

### 3.1 32+2 CRC 物理展开

`0x02001e0a` 按每 32 字节计算 CRC16（多项式 `0x8005`），把每块写成 32 字节
数据加 2 字节 CRC，目标地址为：

```text
physical = 0x02000000 + (logical_offset / 0x20) * 0x22
```

这与 SDK 的 `beken_onchip_crc` 和项目已固定的 32+2 地址域一致。

### 3.2 分区查找、读和 hash

- `0x02002d84`：在官方分区表中按名称查找分区，每个表项步进 `0x40` 字节。
- `0x02002de4`：通过分区 flash device 的 read 回调读取；参数中的分区起点
  按 `(logical + partition_offset) * 0x20 / 0x22` 转成 raw physical 地址。
- `0x02002f1c`：从分区物理末尾前 `0x1100` 的位置读取 `0x60` 字节 RBL header。
- `0x02002e70`：按 header 的 raw-size 字段计算 FNV-1a，并与 header 中保存的值比较。
- `0x02002070`：确认 header magic 为 `RBL\0` 后进入 hash 校验。

因此官方 boot 端的主要检查是 RBL/FNV 完整性和 magic；SDK 下载/打包流程中的
CRC32 不能被推导为发布者签名认证。

### 3.3 首次启动和 A/B 有效性选择

`0x02002304` 在首次启动时分别检查 A、B 是否全为 `0xff`，再检查 RBL magic/hash：

1. A 有效：清除 offset-enable，选择 A；
2. A 无效而 B 有效：写入 `ota_fina_executive`，设置 offset-enable，选择 B；
3. 两者都不满足：记录 `No valid A/B found by rule, fallback jump A`，回退 A。

### 3.4 trial/confirm 状态机

`0x02002728` 从 `ota_fina_executive` 分区读取四个字节：

```text
offset + 0: ota_exec_flag
offset + 4: ota_temp_exec_flag
offset + 8: cust_confirm_flag
offset + 12: ota_download_flag
```

反编译日志字符串明确显示了以下分支：

```text
first start app
do a update b
after do a update b, app not confirm -> next reboot execute partition a
do b update a
after do b update a, app not confirm -> next reboot execute partition b
normal start and exec A
normal start and exec B
```

`0x020023a0` 是进入跳转前的 hash/fallback 状态机：当前槽 hash 失败时验证另一槽；
另一槽成功则写 `ota_fina_executive` 和 offset-enable，并切换到另一槽；两槽都失败
则停止在 WDT/fail path，不继续跳转。

### 3.5 remap 与最终跳转

- `0x020022d8` 读写 Flash-controller `base + 0x64` 的 offset-enable 状态位。
- `0x020024f0` 根据 `appa` 和 `s_app` 的 offset/长度计算并写入 `base + 0x58`、
  `base + 0x5c`、`base + 0x60`。
- `0x020026c4` 读取选中分区的向量表，设置 VTOR/MSP，清理运行环境后间接跳到
  reset handler。
- `0x02002728` 最终调用 `0x020026c4((partition_offset << 5) / 0x22 + 0x02000000)`。

这证实官方 A/B 是一个连续、等长的 CP/AP execute window，通过一个
Flash-controller offset 整体映射到 `s_app`；不是两个独立 CP/AP 槽分别选择。

### 3.6 与 BK7236 Secure-Boot reset/remap 尾部的差异

对 exact v3.1.1.9 A/B binary 的寄存器和调用域重新审计后，成功尾部可以精确写成：

```text
RBL/FNV 选择完成
  -> 0x020024f0：写 Flash-controller 0x44030058/5c/60
  -> 0x02000fe8(0xa000)：配置 main WDT + AON WDT
  -> 设置 0x44030064 的 offset-enable
  -> 0x020026c4：读取目标向量，写 VTOR/MSP，清寄存器，BX reset handler
```

`0x02002728` 的反编译调用顺序直接包含
`0x020024f0 -> 0x02000fe8(0xa000) -> 0x020026c4`。这里的 WDT 是交给下一阶段
接管的启动保护，不是成功路径的跳转机制；函数会继续执行 VTOR/MSP/`BX`。独立的
`0x02001014` 才是清 `0x44000104` 的 bit 0/1、调用 WDT period `6` 并死循环等待
复位的错误路径。

这与 BK7236 Secure-Boot reference 的成功尾部不同：

| 行为 | BK7236 Secure-Boot BootROM reference | BK7258 v3.1.1.9 A/B bootloader |
|---|---|---|
| 成功前验证 | Manifest 签名、镜像 digest、版本策略 | RBL magic/FNV 完整性与 A/B 状态 |
| 映射控制 | 清 `0x44010008` 的 `flash_sel`/`boot_mode` | 写 `0x44030058/5c/60/64` 的 Flash execute-window remap |
| 成功时 WDT | `0xa000`，随后等待 watchdog reset | `0xa000`，随后继续直接交接 |
| 最终控制流 | 无 `BX`/VTOR/MSP 写；由 reset/remap 完成 | 写 VTOR/MSP，清通用寄存器，直接 `BX` |
| 失败闭环 | fail-closed | 独立 WDT-reset helper 或停止 |

对完整 A/B raw binary 的 Ghidra 数据引用、立即数和 literal-pool 审计没有发现
`0x44010008`，也没有发现 BK7236 的 `control_set_to_flash` 调用形态。两者共享的是
WDT 外设 ABI，不是最终交接 ABI。因此不能把 BK7236 的 watchdog-reset 尾部当成
BK7258 官方 A/B bootloader 的实现依据。

作为非基线交叉证据，官方 `bk_idk release/v2.0.1`（commit
`650e754e12fe1e43c37ce2316a973668b033fd48`）中的 BK7258 A/B binary 也完成了
独立反汇编：

| 项目 | v3.1.1.9 基线 | `bk_idk release/v2.0.1` 交叉样本 |
|---|---:|---:|
| size | 18,720 B | 17,632 B |
| SHA-256 | `3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047` | `6d8ee813115ed3148e0a9d5f58156445016a5be6998a0088e76db009de97ccc8` |
| Flash remap | `0x020024f0` | `0x02002274` |
| direct handoff | `0x020026c4` | `0x02002434` |
| main A/B flow | `0x02002728` | `0x02002498` |

新版地址因链接内容变化而移动，但语义顺序不变。其主流程在 `0x020025c8` 调用
remap、在 `0x020025da` 调用相同地址的 WDT helper `0x02000fe8(0xa000)`，最后在
`0x020025fe` 调用 direct handoff。该样本只用于证明官方 non-secure A/B 架构在后续
版本仍保持一致；项目实现和工具约束仍以 v3.1.1.9 为准。

### 3.7 当前 BL1/BL2 交接对照

当前 board-owned 实现与上述 BK7258 ABI 的关键对应关系为：

| 官方行为 | 当前实现 | 结论 |
|---|---|---|
| 写 `0x44030058/5c/60/64` | `bk7258_bl2_remap_secondary()` | 地址和写入顺序一致 |
| begin = A CP 起点 | `0x02010000` | 一致 |
| end = A CP/AP 窗口末端，也是 B pair 起点 | `0x02260000` | 一致 |
| offset = `XIP_BASE + pair_logical_size` | `0x02250000` | 与官方 `appa`/`s_app` 差值公式一致 |
| 设置 VTOR/MSP 后直接跳转 | BL1 `start.S` 与 BL2 `bk7258_bl2_jump()` | 一致，不应改成 watchdog-reset 成功路径 |
| 保留 reset 于 r9，清 r0-r8/r10-r12，`BX r9` | `bk7258_bl2_enter()` | 已按官方尾部补齐 |

BL2 重建后的 `arm-none-eabi-objdump` 在 `0x2802022c..0x2802025a` 确认最终指令序列
为 `mov r9,r1 -> msr MSP,r0 -> DSB/ISB -> 清寄存器 -> BX r9`。本次构建的 raw BL2
为 10,336 字节，CRC padding 后逻辑长度 12,288 字节、物理长度 13,056 字节，未接近
128 KiB BL2 容量边界。

有意保留的差异是：当前 BL1 将 MCUboot BL2 复制到 SRAM，BL2 使用 MCUboot
header/TLV/EC256 验证 CP/AP 成对镜像；官方 non-secure A/B bootloader 自身驻留 XIP，
只验证 RBL/FNV。这个差异属于安全架构分层，不是交接 ABI 偏差。

### 3.8 RBL 与 MCUboot 的 consumer 边界

v3.1.1.9 的 RBL 头是 0x60 字节结构，包含 `RBL\0`、算法、版本字符串、CRC32、
FNV-1a、raw/body size 和 header CRC32。官方 A/B bootloader 从 appa/s_app 分区物理
末端前 `0x1100` 读取它；官方 OTA packager 也用同一字段族生成 OTA 传输容器。这不
表示 BK7258 BootROM 会读取应用 RBL。

当前实际启动链的 consumer 如下：

| 层级 | 消费的格式 | 不消费的格式 |
|---|---|---|
| BK7258 BootROM -> board BL1 | BL1 向量、`BK7236\x10\x00`、32+2 CRC Flash 流 | 应用 RBL、MCUboot TLV |
| board BL1 -> SRAM BL2 | board-owned BL1 Manifest、BL2 向量/范围 | 应用 RBL、应用 MCUboot TLV |
| MCUboot BL2 -> CP/AP | MCUboot `0x96f3b83d` header、SHA-256、EC256 TLV、security counter | RBL/FNV |
| 官方 non-secure A/B bootloader（替代路径） | appa/s_app 尾部 RBL、FNV、trial state | MCUboot header/TLV |

当前已板验构建的 `cp_signed.bin` 和 `ap_signed.bin` 均以 MCUboot magic
`3d b8 f3 96` 开始；`inspect_bk7258_rbl.py` 对这两个成员和完整
`s_app_mcuboot.bin` 均按预期报告“no v3.1.1.9 RBL header found”。工厂包在官方 A/B
可能读取的 A/B tail 地址 `0x284f00`、`0x4f9f00` 处也是 erased `0xff`。与此对应的
实板记录仍达到：

```text
B1PAGE -> B2INIT -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

因此 RBL 不是 BootROM、下载器或当前 board-owned BL1/BL2 链的强制封装。给 MCUboot
CP/AP 镜像再套一层 RBL 只会引入第二套版本/完整性字段和尾部占用，不能增加发布者
认证能力，也没有消费者。当前决定是**不增加 RBL wrapper**；RBL 工具只保留用于
分析官方 A/B/OTA 产物，不进入 MCUboot 发布路径。

### 3.9 register-clean handoff 实板闭环

2026-08-08 使用同一组开发密钥重新生成了完整匹配包，构建配置为官方 SDK
`v3.1.1.9`、NuttX MCUboot `18.1.1`、security counter `18`、12 KiB BL2
逻辑镜像以及 embedded Manifest。BL2 raw 镜像为 10,336 字节；其 32+2 CRC
镜像 SHA-256 为
`f92808f3ff5a846a048011065941fe9e1de6bd25451ac043fef9866cc5e420f3`。

本轮只通过 BKFIL 稀疏写入以下五段：

```text
bl_crc.bin@0x000000-0x011000
bl2_crc.bin@0x51d000-0x004000
bl2_secondary_crc.bin@0x53f000-0x004000
app_crc_flash.bin@0x011000-0x029000
app1_crc_flash.bin@0x165000-0x010000
```

没有写 B 应用槽、LittleFS、N15 metadata、独立 Manifest page、policy、校准区、
OTP 或 eFuse。下载器重启后的 UART 证据位于
`logs/bk7258-auto-debug/20260808-115606`；随后 COM7 RTS 物理复位的独立证据位于
`logs/bk7258-auto-debug/20260808-115650`。两次均达到：

```text
B1PRIMARY -> BL2RAM -> B2INIT -> B2GO -> B2GORET -> B2GOOK
          -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

第二次摘要同时报告 `cold_path=yes`、`bl2_handoff=yes`、`verdict=PASS_NSH`。
日志没有 `HardFault` 或 `ASSERT`。这关闭了本轮从官方 A/B binary 复原出的
register-clean direct handoff 的实板门禁：清理通用寄存器没有破坏 MCUboot
验证、CP 跳转、AP 启动或 NuttX SMP 启动链。

### 3.10 UART 与 watchdog 最终退出闭环

对 `0x020026c4` 的调用链继续下钻后确认，官方 v3.1.1.9 A/B 成功路径在
VTOR/MSP 交接前还执行两步：

1. `0x02000fe8(0xa000)` 重新装载 APB/AON watchdog；
2. `0x02000830 -> 0x02000aa8 -> 0x02000a1c` 等待 UART1 TX 完成，然后按
   `0x1b`、`0x3040`、`0x42` 清 UART1 寄存器，并清系统时钟位
   `0x44010080[15]`、`0x44010030[10]`。

当前 BL2 已按这个顺序实现 `0xa000` watchdog takeover 和 UART1 quiesce，再执行
原有 cache/MPU、VTOR/MSP、register-clean branch。UART 等待保留同一 status bit 17，
但增加有限循环，避免损坏的 UART 永久阻止已认证镜像启动。UART 只在 BL2 -> CP
最终边界关闭；BL1 -> BL2 仍保留串口，以免丢失 MCUboot 诊断。

Arm 构建和 objdump 确认了官方 MMIO 地址、掩码及调用顺序。随后仅稀疏写入：

```text
bl_crc.bin@0x000000-0x011000
bl2_crc.bin@0x51d000-0x003300
bl2_crc.bin@0x53f000-0x003300
```

没有写 CP/AP、B 应用槽、LittleFS、metadata、Manifest data page、policy、校准区、
OTP 或 eFuse。独立 COM7 RTS 复位记录
`/home/lijian/project/open-vela/logs/bk7258-handoff-epilogue/20260808-123750`
报告 `cold_path=yes`、`verdict=PASS_NSH`，并达到：

```text
B1PRIMARY -> B2INIT -> B2GO -> B2GORET -> B2GOOK
-> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

串口 SHA-256 为
`444278ed3522cc54fe2f2896803357dc44b1905c262d46267fae0205bf5d5e9a`；
没有 HardFault 或 ASSERT。关闭 UART1 后 NuttX 能重新初始化并输出 NSH，证明这两项
官方 handoff epilogue 语义与当前 CP/AP MCUboot 链兼容。

### 3.11 fail-closed watchdog reset 闭环

官方 A/B binary 的 `0x02001014` 是独立失败路径：先清
`0x44000104[1:0]`，再通过 `0x02000fe8(6)` 给 APB/AON 两路 watchdog
写入 `0x005a0006`、`0x00a50006`，最后停止在不喂狗的循环中等待硬件复位。
这与成功路径的 `0xa000` application takeover 是两个不同语义。

此前项目 BL1 的致命错误循环反复调用 `boot_wdt_feed()`，因此会永久卡住，和源码
注释所称的“约 8 秒后复位”也矛盾。现在 `boot_wdt_fail_reset()` 复刻上述官方短周期
失败路径，BL1 的 clock、self-test、partition/layout 和双 BL2 均失败出口统一调用它；
BL2 的 `B2BAD` panic 也使用相同闭环。反汇编确认 BL1 和 BL2 都生成了上述状态寄存器、
period 6、双 watchdog key 和无 feed 终止循环。

实板负向验证只临时写入两份 `BL2_SECURITY_COUNTER_FLOOR=0x12010002` BL2；由于 BL2
字节变化而既有 Manifest 保持不变，BL1 正确地以 `rc=2` 拒绝 primary/secondary，
随后 5 秒内重复出现：

```text
B1PRIMARY BAD -> B1SECONDARY BAD -> BAD/no bl2 candidate
-> u_bootloader enter
```

这直接证明 BL1 已从永久错误循环变成 bounded watchdog reset。负向记录为
`/home/lijian/project/open-vela/logs/bk7258-fail-reset-negative/20260808-125704`，
串口 SHA-256 为
`d0b8a553b61f4862d28b32f480f2e8a2dba657273b003b58b22943ba81d83d19`。

随后立即把 package 中的正常 primary/secondary BL2 恢复到 `0x51d000` 和
`0x53f000`。独立 RTS 记录
`/home/lijian/project/open-vela/logs/bk7258-fail-reset-recovery/20260808-125740`
重新达到 `B2HANDOFF -> NuttShell`，`verdict=PASS_NSH`；其串口 SHA-256 为
`444278ed3522cc54fe2f2896803357dc44b1905c262d46267fae0205bf5d5e9a`。
整个试验没有写 boot、CP/AP、B 应用槽、LittleFS、metadata、Manifest、policy、
校准区、OTP 或 eFuse。BL2 自身 `B2BAD` 的硬件触发仍未在本轮单独制造；其实现已完成
编译与反汇编验证，不能把本轮 BL1 负向证据扩大表述为 BL2 panic 实板证据。

### 3.12 Reset_Handler runtime table 分类

官方 `Reset_Handler` 在 cache/TCM 初始化后遍历 `0x020047f4..0x0200480c`
的 12-byte copy table，再遍历 `0x0200480c..0x0200481c` 的 8-byte zero table。
直接解码得到：

```text
copy 0x0200481c -> 0x28000000, 0x3d words (0xf4 bytes)
copy 0x0200481c -> 0x00000004, 0 words       (sentinel)
zero 0x280000f4, 0x0c bytes
zero 0x28000100, 0x2660 bytes
```

被调用的 `0x02003630` 和 `0x0200371c` 分别是通用 `memcpy`、`memset`；因此这两张表
是官方镜像自己的 `.data/.bss` C runtime 初始化，不是额外的 BK7258 硬件初始化 ABI。
当前 BL1 链接脚本强制 `SIZEOF(.data) == 0` 和 `SIZEOF(.bss) == 0`，需要 SRAM 执行的
Flash writer 由其受控入口显式复制，workspace 为 NOLOAD 且不作为持久状态。因此不能
照抄官方表地址或为零长度表强行增加启动代码；本项分类为“架构等价、无需实现”，而
不是待修复缺口。

### 3.13 官方 120 MHz 时钟交接闭环

官方 v3.1.1.9 A/B binary 的 `Reset_Handler` 调用 `0x020006e4(2)`。这里的参数
`2` 是该私有 bootloader helper 的选择值，不能套用 SDK `pm_cpu_freq_e` 的枚举。
沿调用路径和寄存器写入逐条恢复后，该分支等价于：

```text
M1.clkdiv_core = 3
CPU1_HALT_CLK_OP.cpu1_speed = 1
M1.cksel_core = 3
M2.cksel_flash = 1
```

即先把 480 MHz DPLL 除以 4，再切 M1 到 DPLL，形成 120 MHz 交接。该路径不修改
后半句已被源码交叉核对推翻：`M2[25:24]` 是 `cksel_flash`，该路径确实把 Flash
时钟源切到 DPLL。项目此前只完成 DPLL analog sequence，却没有安装这组 M1/CPU1/M2
状态；这是真实缺口，不是注释差异。

board-owned `boot_clock_cold_init()` 现已按官方指令顺序补齐该交接，并且在 DPLL 已开启
的 reset 残留路径也会重新安装确定状态。CP 的初始 DVFS 软件状态同步改为 120 MHz，
避免第一次升频被错误描述成 26 MHz -> 240 MHz。SDK 和 NuttX 源码均未修改。

完整 `cp_nsh_mcuboot` / `ap_smp_mcuboot` 构建通过。反汇编确认 BL1 的 MMIO 写序列为
core divider -> CPU1 speed -> core source mux -> Flash source，CP ELF 中
`g_bk7258_dvfs_cur` 初值为 `3`。本次只写入：

```text
bl_crc.bin@0x000000-0x011000
app_crc_flash.bin@0x011000-0x029000
```

Boot 和 CP SHA-256 分别为
`0d8e1d4cec56746e79d45fe08f154067cfd1e09b764b6ee4ebdce87dd14b28a2` 和
`70649f72e288c487fee76f4a7b71bab5ce325f9f8a2fadb6b0e5b770bb3853ce`。
COM7 RTS 独立复位记录
`/home/lijian/project/open-vela/logs/bk7258-bl1-clock-handoff/20260808-131241`
捕获到：

```text
BClk ... M1=00000033
B1PRIMARY -> BL2RAM -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

其中 `M1[5:4]=3`、`M1[3:0]=3` 是 120 MHz 交接的直接实板证据；串口 raw
SHA-256 为
`c9e54e87c42ca5105dd4b175d7e755540ea5bae71d45e22b67e5396f75170fed`。
启动后 J-Link 只读快照得到 M1=`0x00100020`、M2=`0x05000000`、
`g_bk7258_dvfs_cur=5`；这是当时 `CONFIG_BK7258_CLOCK_320M=y` 的 bring-up
实验结果，不是最终启动频率策略，已被下面 3.14 的 SDK 默认 120 MHz 配置取代。
本轮没有写 AP、BL2、B 槽、LittleFS、metadata、Manifest、policy、校准区、OTP 或
eFuse。

### 3.14 Reset Flash preamble 与 SDK 默认频率策略

官方 Reset 直达链还包括：

```text
boot_mem_check -> close inherited WDT -> flash dual/CRMR reset
               -> JEDEC-ID Flash divider -> 120 MHz clock handoff
```

`boot_mem_check` 已与 v3.1.1.9 `startup_bk7236.c` 指令级交叉确认，访问的是 OTP
controller 的上电/repair 参数窗口与 MEM_CHECK，不是 Flash 配置。Flash helper 将控制器
归一到 dual/continuous-read-reset 状态，并针对 C86516/C86517/C84016/C86018 选择
`M2.ckdiv_flash=1`，其他器件使用保守的 `3`。

官方 A/B binary 从私有 SRAM `0x28000118` 消费预缓存 JEDEC ID，但独立链接的项目 BL1
没有该缓存生产者。实板证明直接读取该地址或尚未触发的 `rd_flash_id` 都会走保守分支。
项目因此复用 v3.1.1.9 `flash_ll_get_id()` 的只读序列：设置 RDID operation、触发
`op_sw`、有界等待 `busy_sw`、读取 `rd_flash_id`，再执行同一 ID 分频表。它不擦除、
编程或修改 Flash 状态寄存器。

同时核对 v3.1.1.9 启动策略：`bk_init()` 让 `PM_DEV_ID_DEFAULT` 投票 120 MHz；模块按需
投票，PM 选择最高请求并通过 `sys_drv_switch_cpu_bus_freq()` 逐档切换。因而当前
`cp_nsh_mcuboot` 不再启用 bring-up 专用 `CONFIG_BK7258_CLOCK_320M`，而是保持 BL1 的
120 MHz 交接并编译 `CONFIG_BK7258_DVFS=y` lower-half，为后续模块投票/governor 接入
保留同一升降压和 mux 顺序。

最终 Boot-only 重刷后，COM7 RTS 冷启动记录
`/home/lijian/project/open-vela/logs/bk7258-bl1-sdk-rdid-active/20260808-134023`
捕获到：

```text
BClk ... M1=00000033 M2=05000000
B1PRIMARY -> BL2RAM -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

串口 raw SHA-256 为
`88a148a9a42b1f70007ea253ddb0c4fb16aa749e46ad38e8151aa0f8a872ef80`。
启动后 J-Link 只读值为 M1=`0x00100033`、M2=`0x05000000`、Flash ID=
`0x00c86517`，证明 NuttX 当前保持官方默认 120 MHz 档，Flash 命中快分频。最终直接
构建的 Boot `bl_crc.bin` SHA-256 为
`c7c65058ad6f7cec0f53649291df41ba0ed6178f0b6ee77968c58789b4e8dbab`；本轮未写 AP、BL2、B 槽、
LittleFS、metadata、Manifest、policy、校准区、OTP 或 eFuse。

### 3.15 Flash busy 超时 fail-closed

Reset Flash preamble 中的两处有界等待现在都使用已有的
`boot_wdt_fail_reset()` 收口：一处等待 dual/CRMR reset 完成，另一处等待主动 RDID
完成。`bl.elf` 的 `boot_flash_reset_prepare` 反汇编显示两个计数器耗尽出口分别位于
`0x02000b4e` 和 `0x02000bac`，最终都调用/跳转到同一个 WDT reset 路径；正常完成路径
不触发该分支。

ARM `-Werror` 的 Boot-only 构建通过。新 `bl_crc.bin` SHA-256 为
`72b329635d66065ca76806aaaab023c0780bff3b8b2ed6fa7085968648471f7c`。仅重刷
`0x00000000..0x00010fff` 后，独立 COM7 RTS 复位记录
`/home/lijian/project/open-vela/logs/bk7258-bl1-flash-timeout-failclosed/20260808-134819`
仍得到：

```text
BClk ... M1=00000033 M2=05000000
B1PRIMARY -> BL2RAM -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

该记录的 `serial.raw` SHA-256 为
`88a148a9a42b1f70007ea253ddb0c4fb16aa749e46ad38e8151aa0f8a872ef80`。
正向启动已实板验证；Flash controller 被强制永久 busy 的负向情形仅完成源码和 ELF
控制流证明，本轮没有为制造该故障而增加测试钩子或脚本，也不把它写成实板负向结论。

### 3.16 Reset runtime/cache/TCM 复核结论

对 exact v3.1.1.9 normal binary 的 `0x02000140..0x02000340` 再次逐指令核对后，
`0x02000228` 被确认只是 Reset_Handler 中装载 copy-table 结束地址的 `ldr r5`，不是
独立 helper。真正的 runtime/cache/TCM helper 是 `0x02000280`：写 VTOR、使能 ITCM/
DTCM，并在存在 I-cache 且尚未使能时执行 invalidate 后置位 `CCR.IC`。Reset_Handler
随后以内联 set/way 循环写 `SCB_DCISW`。

当前 `boot_reset_prepare()` 已覆盖上述语义；`boot_early_soc_init()` 也与官方
`0x02000148` 的 SYS、OTP memory-repair 和 MEM_CHECK 分支一致。因此本项没有新增代码：
强行按地址再添加一个“`0x02000228` helper”反而会重复 runtime 初始化并误读官方 ABI。

### 3.17 Lifecycle journal 与 MCUboot 单一选择者

此前 BL1 已改为只认证/装载 BL2，但 BL2 仍先向 `boot_go()` 暴露两个物理槽，让
MCUboot 按最高版本独立选择。这会绕过 N15 的 consumed-trial/rollback 状态，形成两个
boot-state owner。

现在职责固定为：

```text
BL1: 读取 N15/N17 journal；必要时原子追加 pending -> trial
     -> 通过 0x2801ffd0 checked SRAM record 发布 preferred/fallback
BL2: 单次消费 record
     -> 仅按允许顺序逐对调用 pinned NuttX MCUboot boot_go()
     -> CP/AP 同槽、同版本、同 security counter、向量检查
     -> handoff
```

N15 旧 descriptor/base hash 不再成为第二套镜像接受标准：journal 只决定生命周期顺序，
最终镜像接受仍由 MCUboot EC256/TLV 和现有 CP/AP pair gate 完成。pending->trial 写入若未
通过完整 readback，BL1 只允许稳定 base；TRIAL/ROLLBACK 也不允许回退到已拒绝 target。
handoff 缺失或校验错误时，BL2 保守只尝试 A，不恢复旧的 both-slot scan。

BL1 ARM `-Werror` 构建 text=`23444`、data=`0`，仍在 64 KiB 逻辑预算内；BL2 raw
为 `10708` bytes，继续使用 `0x3000` 签名/CRC padding。最终 Boot SHA-256 为
`407df9447b37c4dc7838d32be1e8a7dc725ddcd9a5f1da9519c217fd919a3958`，BL2 CRC
SHA-256 为
`25119840a165b9fc2196cf8f1540ff239e1ed1d8ad135318eedc3d9108006d0e`。

仅写 Boot 和 primary/secondary BL2 后，独立 RTS 记录
`/home/lijian/project/open-vela/logs/bk7258-bl2-single-policy-owner-source/20260808-141048`
得到：

```text
B1N15 -> B1POLA -> B1PRIMARY -> BL2RAM
       -> B2POLA -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

`serial.raw` SHA-256 为
`682f17551b7e3f10f7b4df3687ce3fede6205393e40e0deca1daa4c0b3baa9ac`，
`verdict=PASS_NSH`。`B1N15` 证明 A 选择来自现有 format-2 journal，而不是固定缺省值。
本轮没有制造 pending 状态，因此没有声称 pending->trial 已在新链上进行实板负向验证。

第一次手工调用 downloader 时曾把多段文件错误地作为多个参数传入，工具在地址
`0x00003000` 报 CRC fail；残留 loader 进程被终止后，完整 Boot 单段恢复成功，再用工具
要求的单个逗号分隔参数写入两份 BL2，均 `WriteFlash -> pass`。最终 RTS 证据来自恢复后
的正确镜像。CP/AP、B 应用槽、LittleFS、metadata、Manifest data partitions、policy、
校准区、OTP 和 eFuse 均未由 downloader 写入。

## 4. 对当前项目的直接影响

1. 当前项目的 N15 连续 CP/AP A/B 布局方向与官方 A/B 数据面一致：应保持成对窗口和
   单 offset 语义，不能把 CP、AP 当成两个独立的 boot 选择器。
2. 当前项目的 board-owned MCUboot BL2 仍是另一层：它负责开发期 MCUboot header/TLV
   和 EC256 签名；官方 A/B binary 本身不能提供这些安全认证能力。
3. 当前 `bootloader/boot_main.c` 的 32+2 raw-to-XIP 转换、A/B layout gate 和
   `bootloader/bl2` 的 MCUboot 验证不能简单替换为官方 A/B binary。两者的 RBL header、
   状态字节、hash 算法和 remap 参数不同。
4. 逆向已补齐官方 A/B 的状态机、时钟和跳转证据，但 BK7258 Secure Boot 的 BL1 manifest/
   授权格式仍缺 exact 官方样本；技术支持已确认“硬件支持，SDK 未适配”。在拿到
   manifest、签名样本或 BL1 secureboot binary 前，不得声称完成官方 Secure Boot 兼容。

## 5. 当前边界与下一步

本记录包含静态逆向和随后的实板闭环，也没有把任何 Ghidra 反编译代码复制进
SDK/NuttX。当前代码对照已经完成：保留 BK7258 的 execute-window remap 和 direct
handoff，不引入 BK7236 watchdog-reset 成功尾部；register-clean、UART quiesce 和
`0xa000` watchdog takeover 和 120 MHz 时钟交接已通过独立 RTS 复位。
RBL/MCUboot consumer 边界也已闭环，不增加 RBL 兼容层。

不在本阶段新增烧录、掉电 campaign 或 SDK 修改。
