# BK7258（涂鸦 T5-AI）openvela / NuttX 移植报告

> 移植目标：Beken BK7258 芯片（Tuya T5-AI 模组，三核 Cortex-M33，Wi-Fi 6 + BLE 5.4）。
> 赛道：openvela 2026 新硬件移植 + AI Coding。

## 摘要

我们在涂鸦 T5-AI 开发板上推进 openvela / NuttX 向 BK7258 的移植。**已完成并板端验证**的工作：
对板上两家 bootloader（涂鸦 65 KB、BK 官方 52 KB）的完整逆向综合；自制 **Tier-1 bootloader**
（asm 跳板 + C main + asm 硬化跳转 epilogue）并在板端跑通 BootROM → bootloader → app 完整跳转链；
用一个最小裸探针（probe）在板端坐实了"启动核 = CPU0"这一关键事实；NuttX 内核完整启动到**交互式
NSH**（Stage N1 跳转链 + N2 NSH console + N3 procfs/ps 均板端验证，2026-07-18）。**配套产出**：
与 Beken 闭源 `cmake_encrypt_crc` **字节等价**的开源 CRC 打包器。

状态速览：✅ Bootloader 逆向 / ✅ Tier-1 bootloader 板端验证 / ✅ 启动核确认 / ✅ CRC packer 等价性
/ ✅ NuttX Stage N1（bootloader 跳进 NuttX，早期 UART）/ ✅ NuttX Stage N2（NSH 交互 console）
/ ✅ NuttX Stage N3（procfs + ps）/ **CURRENT：Stage N4 内 N4-D0/D0D（时钟诊断 baseline + runtime
SysTick bookkeeping）substage 板端验证（feature commit `6f596b7`）；N4-D1（DPLL lock）blocked；DPLL
enable / mux 切换 not attempted；整 N4 not board-verified** / 📋 MTD/FS、Tier-2 bootloader（OTA）、
PSRAM、多核 SMP（后续未编号）。

---

## 1. 背景与目标

**芯片**：BK7258，ARM Cortex-M33 三核（CPU0/1/2），片内 640 KB SRAM、外挂 8 MB PSRAM、
XIP FLASH，集成 Wi-Fi 6 + BLE 5.4。属 BK7236 启动家族（同 BootROM 协议、同 flash CRC 格式）。

**板**：涂鸦 T5-AI 开发板。板上出厂烧的是涂鸦定制 bootloader（基于 BK 官方 core 扩展 OTA +
FAL 分区），app 区由涂鸦私有用例占用。

**竞赛任务**：移植到新硬件。**本阶段目标**：在 BK7258 上打通 BootROM → bootloader → NuttX app
的最小跳转链，先以单核（CPU0）跑出 NSH baseline；多核（CPU1/CPU2 SMP）与 OTA 列入后续路线。

**技术路线选型**：不直接复用涂鸦或 BK 官方的预编译 bootloader binary（vendor blob 不可审计、
带私有 OTA 依赖），而是基于两家共有的 BootROM 启动协议**自制 Tier-1 bootloader**，完全可控
clock / GPIO / WDT / debug / UART，并贴合"移植新硬件"的本意。详见 §5。

---

## 2. 芯片与启动链事实（已确认）

以下事实来自 BK ARMINO SDK (`bk_avdk_smp`) 源码 + 两家 bootloader 二进制逆向 + 板端探针实测，
三方交叉一致。

### 2.1 三核角色

| 核 | 角色 | 证据 |
|---|---|---|
| **CPU0** | **boot master**：BootROM 复位后唯一先跑的核，bootloader 在此核运行并跳 app | SDK 分区表 `primary_cp_app` @ `0x02010000`；`startup_cpu0.c` 的 `entry_main` 即 "app"（master）；多核唤醒 100% 在 app 层（`system_main.c` 的 `start_cpu1_core()`） |
| CPU1 | AP 副核：被 CPU0 的 app 通过 sys_ctrl 寄存器唤醒 | `sys_drv_set_cpu1_boot_address_offset(offset >> 8)` + `sys_drv_set_cpu1_reset(start_flag)`；`startup_cpu1.c` 标 "app@cpu1" |
| CPU2 | AP 副核（可选，`CONFIG_CPU_CNT > 2`） | `startup_cpu2.c` 的 `multicore_launch_core2` |

> 两家 bootloader **都不负责多核唤醒**——这是 app 层 `start_cpu1_core()` 的责任。自制 bootloader
> 因此只需单核跳转即可，显著简化设计。

### 2.2 内存地图

| 区域 | 地址范围 | 说明 |
|---|---|---|
| FLASH XIP | `0x02000000` + | bootloader @ `0x02000000`，app @ `0x02010000` |
| SRAM | `0x28000000` – `0x280A0000` | 640 KB，MSP 顶 = `0x2809FFFC` |
| PSRAM | `0x60000000` | 外挂 8 MB |
| ROM | `0x06000000` | BootROM |
| DTCM | `0x20000000` | 含软件 core-id 约定字（见 §4） |

> 来源：`bk_avdk_smp/cp|ap/include/soc/bk7258/reg_base.h`（`SOC_FLASH_DATA_BASE=0x02000000` 等）。

### 2.3 启动链

```
BootROM (mask ROM)
  → bootloader @ logical 0x02000000  (physical 0x0, CRC-expanded)
  → app        @ logical 0x02010000  (physical 0x11000)
```

### 2.4 FLASH 物理格式（硬件透明 CRC）

```
物理 flash:  [32 B 数据][2 B CRC16][32 B 数据][2 B CRC16]...
逻辑视图:    CPU 看到的连续 32 B 块（flash 控制器硬件透明解码）
地址换算:    physical = (logical / 32) * 34 + (logical % 32)
             0x10000 logical → 0x11000 physical
```

- CRC16：poly `0x8005`、init `0xFFFF`、不反射、big-endian 追加。
- **CRC 校验/解码由 flash 控制器硬件完成**，bootloader / app 不做软件解码——这是 §7 开源 packer
  可替代闭源工具的关键前提。

---

## 3. Bootloader 逆向

对板上两家 bootloader 做了完整反汇编 + SDK 源码交叉引用（`hardware-review-gate` 二进制模式）。
详细文档：

- 综合结论：[`bootloader/full-reverse-synthesis.md`](bootloader/full-reverse-synthesis.md)
- 涂鸦逐函数逆向：[`bootloader/tuya-bootloader-reverse.md`](bootloader/tuya-bootloader-reverse.md)
- BK 官方逐函数逆向：[`bootloader/bk-official-bootloader-reverse.md`](bootloader/bk-official-bootloader-reverse.md)
- 二进制对比：[`bootloader/vendor-bootloader-comparison.md`](bootloader/vendor-bootloader-comparison.md)

### 3.1 结论：同源 + 涂鸦扩展

| 项 | BK 官方 (52 KB) | 涂鸦 (65 KB) |
|---|---|---|
| 二进制 | 不同 binary | 不同 binary |
| 核心启动逻辑 | **同源**（UART1 bring-up / WDT 喂狗 / SWD 使能 / 跳转序列指令级一致） | **同源** |
| Boot magic | `BK7236\x10\x00` @ logical 0x100 | 同（physical 0x110 = logical 0x100 经 CRC 扩展） |
| App magic | `BK7236\0\0` @ logical 0x100 | 同 |
| 分区表 | 末尾 MPC 配置 + 运行时 `partition_get_info()` 查询 | 内嵌 FAL 分区表（`fal_partition` 结构，`bootloader`/`app`/`app1`/`app2`/`download`） |
| 跳转严谨度 | **更严谨**：`flash_cache_disable` + `uart_deinit` + `dsb/isb` + 清 r0-r12 + `bx` | 较朴素 |
| OTA | RBL 头校验（字段完整：magic_ver / header_crc32 / payload_crc32 / sha256） | diff2ya / bspatch 增量 OTA（私有、无源码） |
| 多核唤醒 | **不做**（app 层负责） | **不做**（app 层负责） |

### 3.2 共通的核心启动契约（自制 bootloader 必须复用）

```
1. 关中断 (cpsid i)；喂/关 WDT (0x44000600 / 0x44800010，key 序列 0x5A / 0xA5)
2. (可选) UART1 调试串口 bring-up
3. 读 app 向量表 @ 0x02010000:
     [0x000] = MSP  →  校验 0x28000000 <= MSP <= 0x280A0000
     [0x004] = Reset → 校验 bit0 == 1 (Thumb)
4. 校验 app magic @ 0x02010100:  "BK7236\0\0"
     *(0x02010100) == 0x32374B42  ("BK72" LE)
     *(0x02010104) == 0x00003633  ("36\0\0" LE)
5. 设 VTOR (SCB @ 0xE000ED08) = 0x02010000
6. 设 MSP = app MSP
7. (BK 官方) dsb / isb / 清 r0-r12 / bx app Reset_Handler
```

**关键洞察**：app magic `"BK7236\0\0"` 落在**向量表槽 64/65**（文件偏移 0x100），不是独立头部。
这一点最初从 SDK `startup_cpu0.c` 的注释 *"BK7236 legacy download mode requires that the
flash offset 0x100 is 'BK7236'"* + 两个 LE word 字面量 (`0x32374B42` / `0x00003633`) 确认，
是自制镜像格式设计的关键澄清点（详见 §10）。

### 3.3 magic 偏移 0x110 vs 0x100 的辨析

`vendor-bootloader-comparison.md` 早期记录的 "涂鸦 magic @ 0x110 vs BK @ 0x100" 差异，经
`full-reverse-synthesis.md` 综合后**消解**：两者 magic 在**逻辑**地址都在 0x100；0x110 只是
涂鸦 binary 在**物理**（CRC 扩展后）视图里的偏移（`(0x100/32)*34 = 0x110`）。这不是阻塞项。

---

## 4. 关键决策：启动核（CPU0 vs CPU1）

### 4.1 矛盾的出现

用户上下文早期写 "CPU1 = AP 先行"，而 BK SDK 的 `ap/` vs `cp/` 目录命名含糊（`cp` 里有
`startup_cpu0.c`，`ap/` 里也有 `startup_cpu1.c`），单看目录树判不出哪一核是 boot master。
运行时 core-id 字 @ `0x20000000` 是**软件约定**（上电后由谁写、写什么值取决于 bootloader），
存在自指悖论：读它来判断"首启核号"不可靠。

### 4.2 静态定论：CPU0 = boot master / 跳转核

三条独立证据链：

1. **SDK 分区表**：`primary_cp_app` 分区 @ `0x02010000`，即 bootloader 跳转的 app 区 = CPU0 app。
2. **启动源码命名**：`startup_cpu0.c` 的 `entry_main` 即 "app"（master）；`startup_cpu1.c` 标
   "app@cpu1"（被唤醒的副核）。
3. **多核唤醒位置**：`start_cpu1_core()` 100% 在 app 层（`system_main.c`），bootloader 里没有
   任何 CPU1/CPU2 唤醒代码。反推：bootloader 自己跑的核就是 CPU0。

### 4.3 板端坐实

见 §6——最小探针烧在 `0x02010000`，板端能执行并打印 `vtor=0x02010000`，**能执行本身就证明
bootloader 把它当 CPU0 的 app 跳进来了**。运行时 core-id 字 `0x20000000` 的读数反而是不可靠
的旁证（详见 §6.3）。

> 结论：**bootloader 跳转核 = CPU0**。NuttX baseline 单核先行时，CPU1/CPU2 保持复位，由 NuttX
> app 层将来按需唤醒（与 SDK 一致）。

---

## 5. 自制 Tier-1 Bootloader

### 5.1 为什么自制（不直接烧 vendor binary）

- **完全可控**：clock / GPIO / WDT / SWD debug / UART 全部源码可见，便于 NuttX 联调。
- **无 vendor blob 依赖**：涂鸦 bootloader 带 diff2ya 私有 OTA 引擎（无源码），BK 官方是预编译
  binary；自制摆脱两家私有依赖。
- **小而可审计**：Tier-1 bootloader `text=1353 B`（不含填充），整盘逻辑一个下午可人工复核。
- **贴合赛题本意**："移植新硬件"要求选手掌握启动链每一环，而非把 vendor 黑盒搬上去。

### 5.2 设计哲学：实现两家共有的协议，细节取长补短

Tier-1 bootloader **不基于某一家 binary**，而是实现 BootROM 期望的**通用 app 格式**（两家共有
的启动契约），具体细节取长补短：

- **硬化跳转 epilogue** 学 BK 官方 §2.7（`flash_cache_disable` 除外，见 §5.5）：VTOR → dsb → isb
  → MSP → 清 r0-r12 → `bx`，最严谨的切换序列。
- **FAL 分区表** 用 BK SDK `fal_def.h` 的 `struct fal_partition` 格式（`magic_word=0x45503130`，
  两家通用），按名找 `app`，从 `partition.offset` 推导 app 逻辑地址（非硬编码 `0x02010000`）。
- **init 寄存器序列** 来自板端实测（继承已验证的最小 bootloader `bk7236_min_bl.S`）。

### 5.3 三层混合结构

落盘：[`board/bk7258_t5ai/bootloader/`](../../board/bk7258_t5ai/bootloader/)

| 层 | 文件 | 职责 |
|---|---|---|
| asm 跳板 | [`start.S`](../../board/bk7258_t5ai/bootloader/start.S) | 向量表（64 项）+ bl magic `"BK7236\x10\x00"` @ `.org 0x100` + 逐字保留已验证 init 序列（cpsid / SWD / WDT key / GPIO0/1+GPIO10/11 pinmux / UART1 clk+cfg）+ `bl c_main` + 硬化跳转 epilogue |
| C main | [`boot_main.c`](../../board/bk7258_t5ai/bootloader/boot_main.c) | FAL 分区表解析（按名找 `app`）→ app header 校验（MSP 范围 / Reset Thumb / magic 双 word）→ UART1 日志 |
| asm epilogue | （`start.S` 尾部） | `r1=app MSP`、`r2=app Reset`；`VTOR ← app_vec`；`dsb/isb`；`MSP ← app SP`；`dsb/isb`；清 r0,r1,r3..r12（保留 r2）；`dsb/isb`；`bx r2` |
| 链接 | [`bootloader.ld`](../../board/bk7258_t5ai/bootloader/bootloader.ld) | FLASH @ `0x02000000` slot 0x10000；RAM @ `0x28000000` |
| 打包 | [`bk7236_pack_min_bootloader.py`](../../board/bk7258_t5ai/bootloader/bk7236_pack_min_bootloader.py) | 32+2 CRC 扩展，输出 `bl_crc.bin` + `.json` 元数据 |

> `bss=0` 是刻意设计：`c_main` 只用 `const`（`.rodata`）和栈局部，无需 C runtime 的 `.bss`
> 清零，`start.S` 可直接 `bl c_main`。

### 5.4 Tier-1 特性矩阵

| 特性 | 内容 | 位置 |
|---|---|---|
| **I** UART1 boot 日志 | `u_bootloader enter` / `partition app @ 0x...` / `jump to:0x...` / `JMP`；失败打 `BAD` + 短原因（`msp OOR` / `reset no-thumb` / `magic0` / `magic1` / `no app part`）后死循环 | `boot_main.c` |
| **A** FAL 分区表解析 | 按 `name` 扫表找 `app`，app_vec = `FLASH_BASE + partition.offset`；将来分区表挪到真实 flash 分区也无需改代码 | `boot_main.c` |
| **J** 硬化跳转 epilogue | VTOR / dsb / isb / MSP / 清 r0-r12 / bx（对齐 BK 官方 §2.7） | `start.S` |

### 5.5 刻意偏离规范的地方（有注释、可追溯）

- **`flash_cache_disable` 跳过**：BK 私有 cache 控制块（§2.9，base `0xED00E000`，offsets
  `0x80`/`0x84`/`0x274`）未对照 BK7258 register map 确认，且已验证的最小 bootloader 从不碰
  cache。cache disable 是优化（避免 VTOR 重基后的 stale 取指），不是冷启动交接的正确性要求；
  epilogue 里的 `dsb/isb` 已足够串行化 VTOR + MSP 写。详见 `start.S` 注释块。
- **UART TX poll 有界**：UART1 status (`0x45830018`) bit20 作 "TX-FIFO-not-full" 轮询，busy-wait
  有界（最多 100000 次迭代），若硅片位极性反转则降级为已验证最小 bootloader 的 write-through
  行为，不挂死启动。

---

## 6. 板端验证方法与证据（核心）

方法论：**阶段化去风险**——每一步都在板端验证，最小 bootloader 永远作回退（§6.5）。板上 UART
banner（`BL ... / APP? / OK / JMP`）证明板上已有自制 custom bootloader（`bk7236_min_bl.S`）在跑。

### 6.1 阶段一：最小裸探针（probe）

[`probe/`](probe/) —— 一个 620 B 的最小裸程序，烧到 logical `0x02010000`（physical `0x11000`），
读 core (`0x20000000`) / CPUID (`0xE000ED00`) / VTOR (`0xE000ED08`) 经 **UART1** 打印后死循环。
一次性验证：新写的 linker script、向量表（含 app magic @ 槽 64-65）、UART1 early-print 路径、
bootloader 真实跳转落点。

**镜像自检**（`probe.bin`，烧前验证）：

| 偏移 | 期望字节 | 含义 | 实测 |
|---|---|---|---|
| `0x000` | `fc ff 09 28` | 初始 MSP `0x2809FFFC` LE | OK |
| `0x004` | `61 01 01 02` | `Reset_Handler` = `0x02010161`（Thumb） | OK |
| `0x100` | `42 4b 37 32 33 36 00 00` | app magic `"BK7236\0\0"` LE | OK |

### 6.2 实际板端 UART1 输出（照抄原文）

复位后 UART1 沿用 bootloader 已配好的波特率（即上位机看到 `BL ... / APP? / OK / JMP` banner
的那个波特率）。**实际板端输出**：

```
BL / APP? / OK / JMP        ← custom bootloader (板上已烧的 bk7236_min_bl.S)
BK7258 PROBE
core=0x0AAAAAAA
cpuid=0x0631F132
vtor=0x00201000
HALT
```

> **关于 `vtor=0x00201000` 显示 bug**：实际 VTOR = `0x02010000`。探针 `probe.c` 的 `print_hex32`
> 用 `for (shift = 28; shift >= 0; shift -= 8)` 配合 `v >> (shift+4)` 取高半字节，当 `shift=28`
> 时 `v >> 32` 对 `uint32_t` 是**未定义行为**，最高 nibble 被移丢，`0x02010000` 显示成
> `0x00201000`。Tier-1 bootloader 的 `boot_main.c` 已修复（改用 `for (s = 28; s >= 0; s -= 4)`
> 直接 `v >> s`，不再 `+4`）。底层寄存器值无误——探针确实把自己的向量表写进 VTOR 了。

### 6.3 判读：为什么说"板端坐实 CPU0"

- `core=0x0AAAAAAA`：`0x20000000` 上电随机值（`0xAA` 交替位模式是典型未初始化 SRAM 残留）。
  custom bootloader **不写** `cpu0_set_core_id()`，所以这个字不可用作核号判据。
- **真正证明探针跑在 CPU0 的证据是"探针能执行 + VTOR 一致"**：能执行说明 bootloader 把它当
  CPU0 的 app 跳进来了；VTOR 与写入的 `0x02010000` 一致说明我们的向量表被装载并接管。
- `cpuid=0x0631F132`：BK7258 硅片 SCB->CPUID 实测值（与 ARM 典型 `0x410FC241` 不同，Beken 自定
  义 implementer），证实读到了真实 SCB 寄存器。

### 6.4 阶段二：Tier-1 bootloader 板端输出（照抄原文）

把 Tier-1 bootloader `bl_crc.bin` 烧到 physical `0x0`（bootloader 槽），app 区探针不动。
复位后板端 UART1 输出：

```
u_bootloader enter
partition app @ 0x02010000      ← FAL 分区表解析，从 partition.offset=0x10000 推导，非硬编码
jump to:0x02010000
JMP
BK7258 PROBE ... HALT           ← 硬化跳转 epilogue 落到现有探针
```

### 6.5 验证结论与回退方案

✅ **已板端验证**：

- 跳转核 = **CPU0**（探针能执行 + VTOR 一致，板端坐实）。
- linker script（FLASH `0x02010000` / SP `0x2809FFFC`）正确。
- 向量表 + app magic `"BK7236\0\0"` 落在文件偏移 `0x100`（向量表槽 64/65）被 bootloader 接受。
- UART1 early-print 路径（继承 bootloader 配置，不重写 `UART1_CFG`）。
- bootloader 跳转落点确是我们的 `Reset_Handler`。
- Tier-1 bootloader 的 FAL 分区表解析、app header 校验、硬化跳转 epilogue 全链路板端通过。
- 打包 + 烧录链（`bk7258_crc_expand_app.py` / `bk7236_pack_min_bootloader.py` → `bk_loader`）
  端到端可用。

**回退方案**：若 Tier-1 bootloader 起不来，重烧已验证的最小 bootloader
（`bk7236_min_bl_crc.bin` @ `0x0-0x11000`）即可恢复已知良好基线。整个验证过程没有任何一步
不可回退。

---

## 7. 镜像打包与 CRC

### 7.1 flash CRC 格式（再述）

每 32 字节逻辑数据后追加 2 字节 CRC16（poly `0x8005` / init `0xFFFF` / 不反射 / big-endian），
`physical = (logical / 32) * 34 + (logical % 32)`。**bootrom 严格校验每一块的 CRC**，所以打包器
必须与官方工具字节一致。

### 7.2 开源 packer vs Beken 闭源 `cmake_encrypt_crc`

| 项 | 开源 packer | Beken 闭源 |
|---|---|---|
| app 打包 | `bk7258_crc_expand_app.py`（位于 `zephyr-bk7258-port/tools/`，未入本仓） | `cmake_encrypt_crc` |
| bootloader 打包 | [`bk7236_pack_min_bootloader.py`](../../board/bk7258_t5ai/bootloader/bk7236_pack_min_bootloader.py) | 同上 |
| 源码 | 可审计 | 闭源 |
| 加密 (`-enc`) | **不实现**（baseline 不需要） | 支持 |

### 7.3 等价性已证（任务 #17 验证）

- **参数一致**：poly `0x8005` / init `0xFFFF` / 不反射 / BE，逐项对齐。
- **测试向量匹配**：`0x00 * 32` → `0x8029`；`0xAA * 32` → `0x7FEF`，与闭源工具输出一致。
- **真实 bin byte-identical**：真实 bootloader/app bin + 9 组多样输入，CRC 展开输出与闭源工具
  **逐字节相同**。
- **源码 `crc=0xFFFFFFFF` 32 位写法**：数学等价于规范里的 16 位 `0xFFFF` 初始化（高位仅在 8 次
  循环移位中参与，最终 `& 0xFFFF` 截断），不产生差异。

> 结论：开源 packer 是闭源 `cmake_encrypt_crc` 的**干净等价替代**（不实现 `-enc` 加密，baseline
> NSH 不需要）。这让我们脱离了 Beken 闭源工具链，整个构建路径完全可复现、可审计。

---

## 8. 可移植性

分层设计，让工作可复用到其他 BK7258 / BK7236 家族板：

| 层 | 范围 | 通用性 |
|---|---|---|
| **芯片级** | 芯片目录 + bootloader 核心 + packer | **所有 BK7258 板通用**（启动协议、CRC 格式、UART1 基址、SRAM 地图同芯片同） |
| **板级** | console UART pin、晶振 baud、外部 pin、分区表 | 每块板需各自调板级目录 |
| **跨芯片** | BK7236 家族（BK7236N / BK7256 等） | 作参考模板，**非 drop-in**（地址地图 / 寄存器需核对） |

> 对"别家 BK7258 板"高度可用；目前只 T5-AI 板端验证，同芯片其他板仍需各自实测（板级层差异）。

---

## 9. NuttX BSP 路线（进行中 🚧）

### 9.1 结构模板：`open-vela/vendor_beken`

以官方 `vendor_beken`（BK7236N，同 BootROM 家族）为**结构模板**，参考其：

- **postbuild 流水线**：`CMakeLists.txt` 的 `nuttx_post_build` + `postbuild.sh`，把 NuttX 编译
  产物按 32+2 CRC 扩展。
- **linker**：`/34*32` 虚地址技巧（让 NuttX 链接器看到逻辑视图）。
- **chip / board 分层**：chip 目录放芯片级驱动骨架，board 目录放板级配置。
- **整套驱动骨架**：UART / GPIO / 时钟 / 中断 / flash 等。

### 9.2 关键决策：用我们的开源 packer + 源码 bootloader 集成进 postbuild

比 `vendor_beken` 的**闭源工具 + 预编译 bl** 更干净：postbuild 调用我们的开源 packer 把
`nuttx.bin` 打包成 CRC 扩展格式，产出 `all-app.bin` 整体烧；packer 的输入校验（MSP 范围 /
Reset Thumb / magic）作为构建期检查。**baseline 不做加密**。

### 9.3 阶段化（与 §6 方法论一致）

| 阶段 | 目标 | 状态 |
|---|---|---|
| **N1** | NuttX 最小镜像被 Tier-1 bootloader 跳进去，早期 UART 打印可见 | ✅ done（`board-verified`，commit `40495ca`） |
| **N2** | `nx_start` kernel 起来 + UART1 console → **交互式 NSH** | ✅ done（`board-verified` 2026-07-18，code `9f45bc6` + docs `e3ad3e9`） |
| **N3** | 挂 procfs 到 `/proc` → **`ps` / `ls /proc` / `cat /proc/*` 可用** | ✅ done（code `4d9198e` + docs `68badfe`；state-C `board-verified` 2026-07-18） |
| **N4** | DPLL / 480 MHz CPU0 clock bring-up + 独立测量 + N3 regression | **CURRENT**：N4-D0/D0D substage `board-verified`（feature commit `6f596b7`）；N4-D1 blocked；DPLL enable / mux 切换 not attempted；整 N4 not board-verified（[master](next-stage-prompt.md) / [N4 prompt](nuttx-port/prompts/04-n4-clock-bringup.md) / [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md)） |

N1 判据（已满足）：bootloader 跳进 NuttX 后，NuttX 早期 console 打印出现在 UART1（复用已验证的
UART1 路径）。N2 判据（已满足）：NSH 提示符出现且 `help` / `uname -a` / `echo` / 键盘输入 + 回显
全部可用。N3 判据（已满足）：`ps` 列出 PID 0/1、`ls /proc` 见完整条目集、
`cat /proc/{version,cpuinfo,meminfo}` 返回真实数据。N4 内部按 subsection 分级判据：N4-D0/D0D
（**已 substage 板端验证**）只要求 clock readback + 独立 baseline 测量 + N3 功能回归；只有 DPLL
enable、CPU mux 切换、独立约 480 MHz 测量、UART/SysTick/N3 回归、5 分钟 soak 与 3 次 reboot 全部
完成后，**整 N4** 才能标记 `board-verified`。substage `board-verified` 不向上传染到整 N4。

### 9.4 Stage N2 — 交互式 NSH console（板端验证 2026-07-18）

NuttX 完整启动到交互式 NSH：

```
u_bootloader → JMP → N2 DBESITtC → NuttShell (NSH) → nsh>
nsh> help            （列出全部内建命令）
nsh> uname -a        → NuttX 0.0.0 ... arm bk7258_t5ai
nsh> echo hello      → hello
```

UART1 console 460800 8N1，**RX 中断驱动、TX 轮询**：NSH readline 收键击、解析命令、回显全部 live。
内核侧 `__start` 完成 VTOR + FPU FPCCR（清 bit29/30/31，避免首次异常 lazy-stacking hang）+
`.data/.bss` + `arm_earlyserialinit`，然后 `nx_start()` 起调度器（SysTick 10 Hz + PendSV + SVCall）
+ NSH init 任务。向量表 `slot[15..63] = exception_direct`（真实分派器），SysTick 探针在异常入口
被证明 OK 后还原。NR_IRQS=48 覆盖 UART1 @ slot 31。

**UART1 RX 输入打通 = 4 个叠加 bug 全在 `chip/bk7258_serial.c`，根因链**：

1. **`receive()` 取位错**：原读 `fifo_port & 0xff`（bits[0:7] = TX 字段），RX 字节其实在
   bits[8:15] → 改 `(fifo_port >> 8) & 0xff`。
2. **`CFG.rx_enable` 未开**（`0x45830010` bit1）：Tier-1 bootloader 只 print（TX），留
   `rx_enable=0`。`setup()` OR bit1，保 `clk_div=0x37`（460800）和 `tx_enable`。
3. **三道中断门一道没开**（`rxint` 空函数 / `attach` no-op）：`rxint(true)` 按序开三道 ——
   UART `int_enable`（`0x45830020` bit1）→ 片上中断控制器 `SYS_CPU0_INT_0_31_EN`
  （`0x44010080` bit15）→ NVIC `up_enable_irq(31)`；`attach()` 做 `irq_attach(31, bk7258_uart_isr)`。
4. **RX FIFO 阈值默认 0**（`fifo_config` `0x45830014` bits[8:15]）：`rx_fifo_need_read` 判
   "FIFO ≥ 0" 永远成立 → 一开 RX 立刻 ISR storm。`setup()` 设阈值 = 1。

**UART1 = NuttX IRQ 31**（NVIC 线 15，向量 slot 31 = `exception_direct`），三处一致：startup 向量表
（UART1_Handler @ slot 15）、`icu_map INT_SRC_UART1 → 15`、`SYS_CPU0_INT_0_31_EN` bit15。
注：`ICU_PRI_IRQ_UART1 = 26` 是优先级寄存器索引，**非** NVIC 线号。

详细 worklog：[`nuttx-port/n2-nsh-console.md`](nuttx-port/n2-nsh-console.md)。

### 9.5 Stage N3 — procfs + ps（板端验证 2026-07-18）

在 N2 baseline 上开 procfs：NSH 的 `cmd_ps` 经 `nsh_foreach_direntry(CONFIG_NSH_PROC_MOUNTPOINT,
...)` 枚举 `/proc`，procfs 必须显式挂载（`nsh_initialize` 不自动挂）。N2 没开 `CONFIG_NSH_ARCHINIT`
→ `board_app_initialize()` 是死代码（N2 trace `DBESITtC` 无 `'A'`）。N3 开
`CONFIG_NSH_ARCHINIT`（auto-select `CONFIG_BOARDCTL`，激活 bring-up 钩子）+ `CONFIG_FS_PROCFS`，
在钩子的 `'A'` 标记后 `mount(NULL, "/proc", "procfs", 0, NULL)`，成功发 `'P'`、失败发 `'p'`。
`CONFIG_NSH_PROC_MOUNTPOINT` 经 olddefconfig 解析为 `"/proc"`。

```
u_bootloader → JMP → N2 DBESITtCAP → NuttShell (NSH) → nsh>     ← A = bring-up 钩子入口，P = procfs mount 成功
nsh> ps             → PID 0 IDLE (Kthread, Ready) / PID 1 nsh_main (Task, Running, CPU0)
nsh> ls /proc       → 0/ 1/ cpuinfo fs/ memdump meminfo self/ tcbinfo uptime version
nsh> cat /proc/cpuinfo  → processor :0 / ARMv8-M rev 0 (v8ml) / implementer 0x63 / part 0x132
nsh> cat /proc/meminfo  → total 646144 used 7728 free 638416 maxused 8088
nsh> uname -a       → NuttX 0.0.0 ... arm bk7258_t5ai
```

改动仅 2 文件（全在团队 overlay）：`configs/nsh/defconfig` 新增两行（`CONFIG_FS_PROCFS`、
`CONFIG_NSH_ARCHINIT`；`CONFIG_NSH_PROC_MOUNTPOINT="/proc"` 由默认值解析），
`src/bk7258_bringup.c` 增加 `<sys/mount.h>`、`mount()` 调用与文件头/doc 注释更新。
`nuttx.bin`=88388 B、`all-app.bin`=163574 B（= `bl_crc.bin` 69632 + `nuttx_crc.bin` 93942），
构建零告警。

**两轮验证（证明 verified == committed 源码）**：state-A 镜像（构建时间戳 14:35:19）功能验证通过
后，用提交源码重编 state-C 镜像（时间戳 15:11:55）重刷再验，`/proc/version` 时间戳变化即板上跑
state-C 的证据。NuttX 把 `__DATE__/__TIME__` 烤进版本串导致每次构建哈希不同，但差异仅为构建
时间戳，注释润色对代码生成零影响；以 state-C 复验作为最终验收。

详细 worklog：[`nuttx-port/n3-procfs-ps.md`](nuttx-port/n3-procfs-ps.md)。

### 9.6 Stage N4-D0 / D0D — 时钟诊断 baseline + runtime SysTick bookkeeping（substage 板端验证 2026-07-18）

feature commit `6f596b7`（3 个 overlay 文件：`chip/bk7258_clockdiag.h` 新增、`chip/bk7258_start.c`
接入、`chip/bk7258_timerisr.c` runtime SysTick 选择），**只读诊断 + runtime SysTick bookkeeping**，
**不写** DPLL / CPU mux / clock-control / voltage / flash wait-state / UART divisor 寄存器。DPLL enable
与 CPU mux 切换 **not attempted**（N4-D1 范围）。

诊断发现 CPU0 当前频率依赖**复位路径**，存在两条 baseline：

- **Manual-reset 路径**：`M1=0`、`M2=0`、`dplle=0`、`csrc=0`、`cdiv=0`，`RVR=0x0027AC3F`，板端
  `sleep 10`≈10 s —— 约 **26 MHz XTALH** baseline（DPLL 关、mux 在 XTALH、div /1）。
- **Loader `--reboot 1` 残留路径**（bk_loader 软复位后 D0D 修正前的现象）：`M1=0x423`、
  `M2=0x05000000`、`csrc=2`、`cdiv=3`、`fsrc=1`、`fdiv=1`、`dplle=1`、`vcre=0xB`，`sleep 10`<4 s。
  这些 DPLL/mux 写入**全部来自上位机 loader**（非本移植代码所写）；`6f596b7` 只读这些位。
- **J-Link DWT 独立测频**（loader 路径，2 s 窗口）：`CYCCNT=0x098AA02F` → ≈ **80.04 MHz**，与
  `csrc=2 / cdiv=3 / dplle=1` 残留在量纲上一致；80 MHz ≠ 480 MHz，是 loader 残留副产物，不是 N4 目标。

**N4-D0D runtime fix**：`bk7258_timerisr.c` 检测 loader 残留路径时把 SysTick bookkeeping 切到 80 MHz
档（`RVR=EXP=0x007A11FF`、`HZ=0x04C4B400`、`CLK=1`），硬件时钟源不动；manual-reset 路径保持 26 MHz
档。修正后 loader 路径 `sleep 10` wall-clock deltas 回到 **10.10 s / 11.00 s**，`ps` PID 0/1 与
`/proc/version`（构建时间戳 `Jul 18 2026 22:11:54`）正常。

**候选 artifact**（构建时间戳 `Jul 18 2026 22:11:54`）：`$FW/all-app.bin` = 164730 B = `0x2837A`
（sha256 `9e1d5f19b194c039521611ea495c7ba28a8c9fb90f027979d087d48b7b9b29b6`），`$FW/nuttx.bin` = 89500 B
（sha256 `c13745eb2c86b3426b4cd41d24a17663fc1b3755e96e92cc1204077f9640d999`）。

> **状态边界**：N4-D0/D0D 是 **substage `board-verified`**，仅覆盖 subsection 范围，**不**等价于
> 整 N4 board-verified。**N4-D1（DPLL lock，CPU 保持 XTALH）目前 blocked**：当前 loader 路径已有
> `dplle=1` 残留，无法干净区分"本移植写的 DPLL enable"与"loader 残留"，需先在 manual-reset 冷启动
> 路径上重做 baseline 再申请 D1 mutation 授权。DPLL enable、CPU mux 切换、320/480 MHz 目标**均未
> 执行**。
>
> **Loader residue 来源**：loader `--reboot 1` 的软复位把 NuttX 交接到一个**已初始化的时钟状态**，
> 该状态与 Beken SDK CP 早期初始化残留一致（SDK 参考路径：`Reset_Handler_Cpu0` →
> `sys_drv_early_init()` → `sys_hal_early_init()`）。当前 NuttX overlay **不移植也不调用**
> 该初始化链；它只**读取继承的寄存器状态**（M1=0x423、M2=0x05000000、dplle=1 等），并据此补偿
> SysTick bookkeeping。SDK 的 `sys_hal_early_init` 包含 analog batch writes 和 mux/divider
> side effects 且没有 positive DPLL locked-bit 断言，不能直接照搬为 N4-D1 的受控 lock-only
> sequence。
>
> **Caveat（exact-commit rebuild）**：上述 artifact 对应**当时构建的候选镜像**（功能 + wall-clock
> 回归已板上验证）。**最终以精确 commit `6f596b7` 重编 + 重刷的 state-C 复验尚未完成**——这是
> N4-D0 整体收口的剩余项；在 state-C 复验并确认 SHA-256 匹配前，不应将整 N4 标记为 board-verified。

详细 worklog：[`nuttx-port/n4-d0-clock-diag.md`](nuttx-port/n4-d0-clock-diag.md)。

### 9.7 Stage N4 后续 handoff — DPLL / 480 MHz 时钟 bring-up

BK7258 官方核心路径为 26 MHz XTALH → DPLL（320 / 480 MHz）→ CPU core；当前
`BOARD_CPU_FREQ_HZ=26000000`，自制 Tier-1 bootloader 未配置 DPLL。`/proc/cpuinfo` 的
`cpu MHz : 0.000` 不能证明当前频率，N4-D0 已用寄存器 readback + 独立测量建立 baseline
（manual-reset 约 26 MHz / loader 残留约 80 MHz）；后续 N4-D1 起才按 vendor sequence 安全切到
CPU0 480 MHz，并回归 UART1、SysTick、NSH 与 procfs/ps。

- 主 Stage 索引 / current pointer：[`next-stage-prompt.md`](next-stage-prompt.md)
- 当前完整 Stage N4 prompt：[`nuttx-port/prompts/04-n4-clock-bringup.md`](nuttx-port/prompts/04-n4-clock-bringup.md)

N4-R → N4-D0 → N4-D1 → N4-D2（optional）→ N4-D3 → N4-V 是一个 MAIN Stage 文件内的有序
subsection。N4-D0/D0D 已 substage 板端验证，N4-D1 blocked，D2/D3/V 尚未开始。

MTD / 文件系统、PSRAM、Tier-2 bootloader OTA、SMP 均属于 N4 之后的未编号工作；只有前一 Stage
板端证据明确后，才确定下一 MAIN Stage 的范围与 prompt。

---

## 10. AI 协作过程（AI Coding 赛道加分）

### 10.1 方法论

- **苏格拉底式澄清**：范围 / 意图不清时先问，不臆造。例：用户上下文写"CPU1 先行"与 SDK 矛盾，
  不强行硬编 CPU1，而是深挖 SDK 源码 + 板端探针把矛盾消解（§4）。
- **阶段化板端验证**：探针 → Tier-1 bootloader → NuttX，每步可回退。最小 bootloader 永远保留
  作回退基线。
- **主模型规划 / 审核 + 委托 subagent 执行**：搜索、机械编辑、重复验证批量委托 subagent；架构
  决策与证据审核由主模型把关。
- **授权门禁**：构建、打包、刷机、PR 均需用户明确授权；AI 不擅自烧录或推送。
- **严格区分 ✅已板端验证 / 🚧进行中 / 📋规划中**：板端才算数，逆向推断和静态分析明确标注。

### 10.2 关键澄清点举例

| 澄清点 | 误区 | 正解（证据驱动） |
|---|---|---|
| 启动核 | "CPU1 = AP 先行"（用户上下文） | **CPU0 = boot master**（SDK 分区表 + 启动源码命名 + 多核唤醒在 app 层 + 板端探针） |
| magic 位置 | 独立头部 | **在向量表槽 64/65**（文件偏移 0x100），SDK `startup_cpu0.c` 注释 + LE word 字面量 |
| UART config | 探针自己重配 `UART1_CFG` | **继承 bootloader 配置**不重写（重写会与时钟分频冲突，看不到输出；Zephyr `soc_reset_hook` 实测可打印） |
| magic 偏移 | "涂鸦 0x110 vs BK 0x100 阻塞" | **都是逻辑 0x100**，0x110 是物理视图（CRC 扩展），非阻塞 |
| 开源 packer | 必须用闭源 `cmake_encrypt_crc` | **开源 packer 字节等价**（§7.3），脱离闭源工具链 |

---

## 11. 产物清单与提交

### 11.1 文件树

```
docs/bk7258-t5ai/
  README.md                              本目录索引（本次重写为简洁版）
  porting-report.md                      ★ 本报告（主文档）
  sdk-context-index.md                   BK ARMINO SDK 上下文索引
  next-stage-prompt.md                   MAIN Stage 索引 / current handoff
  bootloader/
    full-reverse-synthesis.md            两家 bootloader 逆向综合
    tuya-bootloader-reverse.md           涂鸦逐函数逆向
    bk-official-bootloader-reverse.md    BK 官方逐函数逆向
    vendor-bootloader-comparison.md      二进制对比
  probe/
    README.md / probe.c / probe.ld / Makefile   板端验证探针
  nuttx-port/                            NuttX 移植 worklog
    n2-nsh-console.md                    Stage N2 会话记录（boot trace + 4 RX bug + 验证证据）
    n3-procfs-ps.md                      Stage N3 会话记录（procfs 挂载 + ps/ls/cat 板端验证）
    n4-d0-clock-diag.md                  Stage N4-D0/D0D 会话记录（时钟诊断 + runtime SysTick + N4-D1 blocker）
    prompts/
      04-n4-clock-bringup.md             当前 MAIN Stage N4 完整恢复提示词

board/bk7258_t5ai/bootloader/
  start.S                                asm 跳板 + 硬化 epilogue
  boot_main.c                            C main：FAL 解析 + header 校验 + 日志
  bootloader.ld                          FLASH @ 0x02000000
  Makefile                               arm-none-eabi-gcc freestanding
  bk7236_pack_min_bootloader.py          开源 CRC 打包器
  README.md                              bootloader 说明
  (bl.bin / bl.elf / bl_crc.bin / *.o / *.map / *.json — 构建产物，已 .gitignore)
```

### 11.2 提交记录

| commit | 分支 | 内容 |
|---|---|---|
| `6f596b7` | `contest2026-multi-board` | feat(bk7258): add N4-D0 clock diagnostics（read-only 诊断 + runtime SysTick bookkeeping，3 overlay 文件；N4-D0/D0D substage 板端验证） |
| `68badfe` | `contest2026-multi-board` | docs(bk7258): Stage N3 board-verified — procfs + ps |
| `4d9198e` | `contest2026-multi-board` | feat(bk7258): NuttX Stage N3 — procfs + ps（board-verified） |
| `e3ad3e9` | `contest2026-multi-board` | docs(bk7258): Stage N2 board-verified — NSH interactive console |
| `9f45bc6` | `contest2026-multi-board` | feat(bk7258): NuttX Stage N2 — NSH interactive console（board-verified，14 文件 +1579/-164） |
| `40495ca` | `contest2026-multi-board` | feat(bk7258): NuttX Stage N1 — minimal image boots via Tier-1 bootloader（board-verified） |
| `ceead19` | `contest2026-multi-board` | feat(bk7258): board-verified probe + Tier-1 bootloader（**11 文件，+1296 行**） |
| `783e049` | `contest2026-multi-board` | docs(bk7258): complete bootloader reverse-engineering（Tuya + BK 官方） |

构建产物（`*.bin` / `*.elf` / `*.map` / `*.o` / `__pycache__` / `bl_crc.json`）已 `.gitignore`，
可从源码一键复现（`make` + `python3 bk7236_pack_min_bootloader.py`）。

### 11.3 回退基线

最小 bootloader `bk7236_min_bl_crc.bin`（位于 `zephyr-bk7258-port/out/custom_bootloader/`）
作为已知良好回退镜像，烧 `@0x0-0x11000` 即可恢复。

---

## 12. 下一步 Roadmap

| 优先级 | 项 | 状态 | 备注 |
|---|---|---|---|
| P0 | **NuttX Stage N1**：最小 NuttX 镜像被 bootloader 跳进去，早期 UART 打印可见 | ✅ done | `board-verified`，commit `40495ca` |
| P0 | **NuttX Stage N2**：`nx_start` + UART1 console → **交互式 NSH** | ✅ done | `board-verified` 2026-07-18，code `9f45bc6` + docs `e3ad3e9` |
| P0 | **NuttX Stage N3**：挂 procfs 到 `/proc` → `ps` / `ls /proc` / `cat /proc/*` | ✅ done | code `4d9198e` + docs `68badfe`；state-C `board-verified` |
| P0 | **NuttX Stage N4**：DPLL / 480 MHz CPU0 clock bring-up | **CURRENT**：N4-D0/D0D substage `board-verified`（`6f596b7`）；N4-D1 blocked | N4-R → N4-D0 ✅ → N4-D1（blocked）→ N4-D2（optional）→ N4-D3 → N4-V；[master](next-stage-prompt.md) / [prompt](nuttx-port/prompts/04-n4-clock-bringup.md) / [D0 worklog](nuttx-port/n4-d0-clock-diag.md)；DPLL enable / mux 切换 not attempted，整 N4 not board-verified |
| P1 | **后续（未编号）**：MTD + 文件系统（LittleFS / SmartFS） | planned later | N4 证据完成后再决定是否成为下一 MAIN Stage |
| P1 | **后续（未编号）**：PSRAM bring-up | planned later | T5-AI 16 MB SiP PSRAM 当前未用 |
| P1 | **后续（未编号）**：Tier-2 bootloader OTA（RBL + A/B + failover） | planned later | 需 flash 写；参考 BK 官方 §2.12 RBL 校验 |
| P2 | **后续（未编号）**：CPU1 / CPU2 唤醒 + NuttX SMP | planned later | app 层 `start_cpu1_core()`；N4 明确排除 SMP |
| P2 | **后续（未编号）**：GPIO / flash / Wi-Fi / BLE 等驱动补全 | planned later | 根据 N4 后的板端证据与竞赛优先级再排序 |

---

*本报告所有技术断言可追溯：逆向结论指向 `docs/bk7258-t5ai/bootloader/*.md`，源码指向
`board/bk7258_t5ai/bootloader/*` 与 `docs/bk7258-t5ai/probe/*`，提交指向 git 历史。板端 UART
输出为照抄原文，未做修饰。*
