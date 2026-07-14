# RV1126B HPMCU openvela/NuttX 移植 — 阶段记录

- **原始记录日期：** 2026-07-12
- **记录性质：** Phases 01–04 的历史开发时间线
- **当前状态（2026-07-14）：** Phase 04 的 UART RX/interactive NSH 目标已完成并板测；Phase 05 是当前的基线保护、文档同步、候选重建与补充证据工作。

> [!IMPORTANT]
> 本文保留早期开发细节。当前技术事实以 [RV1126B NSH port guide](../rv1126b-nsh-port.md) 为准，板测结论以不可变的 [2026-07-14 NSH 基线证据](../verification/2026-07-14-rv1126b-nsh-baseline.md) 为准；不要将本文中的临时 polling、调试字符或“待验证”表述当作当前实施说明。

---

## 项目概述

- **目标平台：** RV1126B HPMCU（RISC-V）
- **目标系统：** openvela / NuttX
- **已验证串口路径：** UART5 M0，GPIO4_PA6/GPIO4_PA7，1.5 Mbaud，8N1，raw IRQ 61，INTMUX group 1 bit 29
- **内存布局：** RAM 起始 0x48c02000, 大小 0x3a000（约 232KB）
- **已验证构建后端：** classic Make；CMake 不等价且未板测
- **构建命令：** `cd "$WORKSPACE" && ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8`

## 阶段状态修正

- Phase 01（最小构建修复）、Phase 02（镜像打包与首次启动）、Phase 03（DCache/启动与 NSH TX）、Phase 04（UART RX 与 interactive NSH）均为历史阶段。
- Phase 04 的结果为：boot、banner、prompt、RX、`help` 和 prompt return 已板测。
- `uname -a`、board revision、exact flash command 和 timestamped capture 仍没有基线证据。
- 当前请使用 [Phase 05](prompts/phase-05-verified-baseline-follow-up.md)；旧提示词仅保留作历史上下文。

## 更正后的关键文件路径（contest overlay 内）

- 汇编入口：`board/contest_board/chip/rv1126b_head.S`
- 芯片启动：`board/contest_board/chip/rv1126b_start.c`
- IRQ/IPIC 初始化：`board/contest_board/chip/rv1126b_irq.c`
- IRQ 分发：`board/contest_board/chip/rv1126b_irq_dispatch.c`
- UART5 early console / serial：`board/contest_board/chip/rv1126b_lowputc.c`、`board/contest_board/chip/rv1126b_serial.c`
- atomic shim：`board/contest_board/chip/rv1126b_atomic.c`
- 板级 defconfig：`board/contest_board/configs/nsh/defconfig`
- 板级启动 / late bring-up：`board/contest_board/src/rv1126b_boot.c`、`board/contest_board/src/rv1126b_bringup.c`

## 阶段提示词

- [Phase 01 — build fix](prompts/phase-01-build-fix.md)
- [Phase 02 — boot verification](prompts/phase-02-boot-verify.md)
- [Phase 03 — DCache/startup verification](prompts/phase-03-dcache-fix-verify.md)
- [Phase 04 — UART RX restoration (historical)](prompts/phase-04-nsh-uart-rx-restore.md)
- [Phase 05 — verified-baseline follow-up](prompts/phase-05-verified-baseline-follow-up.md)

## 协作规则

- 主模型（Opus）：规划、审核、决策
- 子代理（Sonnet/Haiku）：查代码、执行构建、机械性修改
- 查代码优先使用 CodeGraph（`projectPath=/home/lijian/project/open-vela`）
- 所有修改限制在 `contest2026_135_yongwangzhiqian/` overlay 目录内

---

## 进展日志

### 2026-07-12 — 阶段 1 完成

**最终产物：** nuttx ELF 32-bit RISC-V, entry 0x48c02000, 86300 bytes（36.33% of 232KB RAM）

**修复的关键问题清单：**

| # | 文件 | 修复内容 |
|---|------|----------|
| 1 | `scripts/Make.defs` | 补齐 `Config.mk` + `Toolchain.defs` include，设置 `CROSSDEV=riscv-none-elf-` |
| 2 | `chip/include/irq.h` | 定义 `NR_IRQS = RISCV_IRQ_EXT + 256`（INTMUX 8×32） |
| 3 | `chip/include/chip.h` | 最小壳头文件 |
| 4 | `chip/Make.defs` | 改为 include `common/Make.defs`，过滤掉 `riscv_mtimer.c` |
| 5 | `src/Makefile` | 改为 `$(TOPDIR)/Make.defs` + `Board.mk` |
| 6 | `defconfig` | 移除无效 Kconfig 符号，添加 `RELPATH=y` |
| 7 | `rv1126b_config.h` | 使用 `CONFIG_RV1126B_UART5_CONSOLE` |
| 8 | `rv1126b_head.S` | `_start` → `__start`，trap frame 修正为 NuttX 标准布局（EPC+31 regs+mstatus） |
| 9 | `rv1126b_irq_dispatch.c` | 返回 `void*`，通过 `riscv_doirq` 分发，移除 `board_reset` |
| 10 | `rv1126b_irq.c` | 移除重复的 `up_irq_save/restore`，添加 `up_irq_enable` 实现 |
| 11 | `rv1126b_serial.c` | 移除 `TCIOBAUDRATE`，修复 `up_putc` 返回类型 |
| 12 | `rv1126b_timerisr.c` | 添加 `chip.h` include |
| 13 | `rv1126b_atomic.c` | 新增，提供 GCC atomic 内建函数（无 libatomic） |
| 14 | `rv1126b_bringup.c` | 新增 `board_app_initialize` 桩函数 |

**阶段 1 恢复提示词：** [`prompts/phase-01-build-fix.md`](prompts/phase-01-build-fix.md)

---

### 2026-07-12 — 阶段 2 进行中

**目标：** 板端验证 — 通过 UART5M0 看到 NSH 输出

**阶段 2 恢复提示词：** [`prompts/phase-02-boot-verify.md`](prompts/phase-02-boot-verify.md)

#### 镜像打包

- 通过 `riscv-none-elf-objcopy -O binary` 生成 nuttx.bin (80,304 bytes)
- 使用 SDK 的 `mkimage -f amp.its -E -p 0xe00` 打包为 FIT 格式 amp.img (84,992 bytes)
- 已重新打包 update.img (1.35 GB)
- 烧录方式：`upgrade_tool di amp amp.img`（单独烧 amp 分区）或 `upgrade_tool uf update.img`

#### 首次烧录结果

U-Boot 成功加载固件（SHA256 OK, 80304 bytes → 0x48c02000），但 UART5 无任何输出。

U-Boot 关键日志：
```
Handle standalone: 'hpmcu' at 0x48c02000 ...OK
```
确认 U-Boot 通过 ATF SMC 释放 HPMCU 复位，HPMCU 应在执行 NuttX 代码。

#### 发现的 Bug：DCache 寄存器偏移和位域全部写错

**文件：** `board/contest_board/chip/rv1126b_start.c`

| 寄存器/位域 | 错误值 | 正确值 | 来源 |
|---|---|---|---|
| `GRF_HPMCU_CACHE_ADDR_START` | +0x0204 | **+0x001C** | SDK rv1126b.h GRF_SYS_REG 结构体 |
| `GRF_HPMCU_CACHE_ADDR_END` | +0x0208 | **+0x0020** | SDK rv1126b.h GRF_SYS_REG 结构体 |
| `DCACHE_CACHE_STATUS` | +0x0004 | **+0x0030** | SDK DCACHE_REG 结构体 |
| `DCACHE_STB_TIMEOUT_CTRL` | +0x0008 | **+0x000C** | SDK DCACHE_REG 结构体 |
| `DCACHE_CACHE_ENTRY_THRESH` | (7<<4) | **(7<<8)** | SDK 位域定义 bits[10:8] |
| `DCACHE_STB_TIMEOUT_EN_BIT` | (1<<8) | **(1<<7)** | SDK 位域定义 bit 7 |
| `DCACHE_CACHE_BYPASS_BIT` | (1<<16) | **(1<<6)** | SDK 位域定义 bit 6 |

**影响：** DCache init 读写错误地址，可能卡死或未正确配置 uncache region，导致所有外设寄存器访问经过缓存，UART 写入永远到不了硬件。

**修复状态：** ✅ 已修复并验证。

#### 阶段 2 关键发现汇总

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| 1 | `rv1126b_start.c` | DCache base: 0xff4b0000/0xff4e0000 | → 0x20100000/0x209D0000 |
| 2 | `rv1126b_start.c` | DCache 偏移/位域全部错 | 见上表 |
| 3 | `rv1126b_lowputc.c` | GPIO/CRU 写入未用 high-word mask | 改为 RK hiword 写法 |
| 4 | `rv1126b_clockconfig.c` | GPLL lock bit 检查错（CON0 bit31） | → CON2 bit10 |
| 5 | `rv1126b_timerisr.c` | 缺 `nxsched_process_timer()` | 已添加 |
| 6 | `rv1126b_irq_dispatch.c` | ecall 未推进 mepc | 改走 `riscv_doirq()` |
| 7 | `rv1126b_serial.c` | TX interrupt investigation | Historical intermediate workaround; not the final runtime model |
| 8 | `rv1126b_idle.c` | RX interrupt investigation | Historical proposal only; no current `rv1126b_idle.c` is part of the BSP and normal RX is not idle-loop polling |

**2026-07-13 凌晨（历史状态）：** NuttX 成功启动，输出 `NuttShell (NSH)` 横幅。当时 RX 仍待修复。

---

### 2026-07-14 — Phase 04 final status and preserved baseline

UART RX was restored through the established UART5 M0 / INTMUX / IPIC route. The board-tested result confirms boot, banner, `nsh>` prompt, interactive RX, `help`, and prompt return. The implementation uses raw IRQ 61 through INTMUX group 1 bit 29 with IPIC init/SOI/EOI; normal runtime RX/TX is interrupt-driven.

- DCache remains bypassed in this verified baseline.
- Classic Make is the verified build backend; CMake is not a claimed equivalent.
- `uname -a`, board revision, exact flash command, and a timestamped capture remain pending evidence and must not be claimed.
- The formal record, including immutable artifact identity and the observed `help` transcript, is [2026-07-14 RV1126B NSH baseline evidence](../verification/2026-07-14-rv1126b-nsh-baseline.md).
- Next work is [Phase 05](prompts/phase-05-verified-baseline-follow-up.md): preserve the baseline, document candidate provenance, rebuild only with classic Make, and obtain the remaining hardware evidence.
