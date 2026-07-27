# Beken BK7258（Tuya T5-AI）openvela / NuttX 移植

把 openvela / NuttX 移植到 Beken BK7258（ARM Cortex-M33 三核、Wi-Fi 6 + BLE 5.4）Tuya T5-AI
模组。BootROM → Tier-1 bootloader → CPU0/CP NuttX、NSH、LittleFS、CPU0 IRQ/GPIO 等既有
阶段已有板端证据。当前 **Stage N7** 已完成物理 CPU1 独立单核 AP NuttX 原型、CPU0
start/stop/restart 控制、共享 SRAM/raw mailbox、LittleFS-safe 双镜像打包，以及 CP/AP SDK
role 静态库集成。同步后完整 CP→AP→CP restore 构建通过，状态为 `build-verified`；当前
`CONFIG_BK7258_AP_AUTOSTART=y`，但尚未烧录和板测，仍不是最终 AP wrapper 架构。CPU1
执行、AP_READY、运行时诊断、CPU0 回归和重复启停不得提前表述为 `board-verified`。

> 详细技术报告（评委请读这份）：**[porting-report.md](porting-report.md)**
> N2 worklog：[`nuttx-port/n2-nsh-console.md`](nuttx-port/n2-nsh-console.md)
> N3 worklog：[`nuttx-port/n3-procfs-ps.md`](nuttx-port/n3-procfs-ps.md)
> N4-D0/D0D worklog：[`nuttx-port/n4-d0-clock-diag.md`](nuttx-port/n4-d0-clock-diag.md)
> N5 flash filesystem worklog（D5 raw flash r/w + D6 MTD + D7 LittleFS，board-verified 2026-07-19）：[`nuttx-port/n5-flash-filesystem.md`](nuttx-port/n5-flash-filesystem.md)
> N7 CPU1/AP 单核启动链 worklog（build-verified，未板测）：[`nuttx-port/n7-ap-singlecore-bringup.md`](nuttx-port/n7-ap-singlecore-bringup.md)
> Git worktree 同步与 PR 交接记录：[`nuttx-port/git-worktree-sync-2026-07-27.md`](nuttx-port/git-worktree-sync-2026-07-27.md)
> 主 Stage 索引 / 当前恢复入口：[`next-stage-prompt.md`](next-stage-prompt.md)

## 当前状态

| 工作项 | 状态 |
|---|---|
| 两家 bootloader 完整逆向（涂鸦 + BK 官方） | ✅ 已板端交叉验证 |
| Tier-1 bootloader（asm + C + 硬化跳转） | ✅ 板端验证 |
| 启动核 = CPU0（关键决策） | ✅ 板端坐实 |
| 开源 CRC packer（闭源 `cmake_encrypt_crc` 等价替代） | ✅ 字节等价已证 |
| NuttX Stage N1（bootloader 跳进 NuttX，早期 UART） | ✅ `board-verified` |
| NuttX Stage N2（`nx_start` → 交互式 NSH） | ✅ `board-verified`（2026-07-18，4 RX bug 全修） |
| NuttX Stage N3（procfs + `ps`） | ✅ `board-verified`（2026-07-18） |
| NuttX Stage N4（DPLL / 480 MHz clock bring-up） | 历史：N4-D0/D0D/D0F `board-verified`（substage，D0/D0D `6f596b7`，D0F `8dab594`）；N4-D1 blocked；当前产品路径采用已验证的 320 MHz runtime DVFS，不继续追 480 MHz |
| NuttX Stage N4 — D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping） | ✅ substage `board-verified`（2026-07-18，feature commit `6f596b7`，3 个 overlay 文件） |
| NuttX Stage N4 — D0F（100Hz SysTick tick-rate 兼容性） | ✅ substage `board-verified`（2026-07-18，feature commit `8dab594`，defconfig 移除 100ms override） |
| NuttX Stage N5（flash layout / ID / filesystem） | **N5-D0..D4 board-observed**（2026-07-19）；**N5-D5 raw flash r/w board-verified**（2026-07-19）；**N5-D6 MTD board-verified**（方案 A，CONFIG_BK7258_FLASH_MTD）；**N5-D7 LittleFS filesystem board-verified**（/data 挂载，probe 文件重启持久化通过）；D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`，boot/app 区不受影响） |
| NuttX Stage N6（CPU0 SDK IRQ/GPIO） | CPU0 vectors、TIMER1 IRQ 与 GPIO C0/C1/C2 已有板端验证；作为 N7 回归基线保留 |
| **NuttX Stage N7（CPU1 独立单核 AP NuttX）** | **CURRENT：AP 原型 + CP/AP SDK role 静态库集成 `build-verified`，未板测，非最终 wrapper 架构**；`AP_AUTOSTART=y`，用户板测后继续 AP wrapper 收口 |
| MTD / 文件系统 | ✅ board-verified（N5-D6 MTD + N5-D7 LittleFS，/data 挂载） |
| NuttX Stage N6-A1（SDK integration + 80-slot RAM vectors） | ✅ board-verified（VTOR `0x28000800`，magic slots 64/65 与运行期 vector repair 均通过） |
| 4295 秒系统时间折返修复 | ✅ board-verified（`CONFIG_SYSTEM_TIME64=y`，uptime 单调增长到 5834.58 秒，无 HF/WDT 复位） |
| NuttX Stage N6-B（CPU0 SDK IRQ bridge） | ✅ TIMER1/source-3/IRQ19 board-verified（两次独立启动、三次 `bkirqtest` 全 PASS；静态 verifier 48/48 PASS） |
| GPIO foundation C0 | ✅ board-verified：P9 active-high LED + P29 active-low USERKEY，3 个独立 boot/download、5 次 `bkgpioc0` PASS |
| GPIO C1/C2 | ✅ board-verified：GPIO_NS source37/IRQ53 与 CPU0 group2 gate 已验证；`/dev/gpio0`/`/dev/gpio1` lower-half 完成，两次连续 falling-edge 命令通过；保留 `CONFIG_DEV_GPIO_NSIGNALS=2` 规避 upstream unregister 缺陷 |
| 下一阶段 | 成套烧录 boot/CP/AP，验证 AP autostart、`apctl` 生命周期和 CPU0 回归；随后继续 AP wrapper 收口 |
| Tier-2 bootloader（OTA / A-B failover） | 后续，未编号 |
| 多核后续 | CPU1 单核 AP 为 N7 build-verified；物理 CPU2、AP SMP、RPTUN 与服务层均后续 |

**N7 构建产物**：`$FW/bk7258-dual/app.bin`（CP，171956 B）、`app1.bin`（AP，64346 B）及
`bk7258-dual-image.json`。正常更新使用 `bl_crc.bin@0x0-0x11000`、
`app_crc.bin@0x11000-0x2c9bc`、`app1_crc.bin@0x220000-0x10b16` 三个 segment，保留
logical `0x100000..0x1fffff` 的 LittleFS；
`all-app-factory.bin` 会 padding/擦除该数据区。root `$FW/all-app.bin` 继续只是
bootloader + CP 的兼容镜像，不包含 AP；builder 已验证它与 root/manifest CP 一致。
`$FW = $WORKSPACE/nuttx`，console UART1 460800 8N1。

## 产物索引

### 主报告
- **[porting-report.md](porting-report.md)** —— 评委可读的详细移植报告（背景 / 芯片事实 / 逆向 /
  Tier-1 bootloader / 板端验证 / CRC packer / NuttX 路线 / AI 协作 / Roadmap）

### Bootloader 逆向（`bootloader/`）
- [full-reverse-synthesis.md](bootloader/full-reverse-synthesis.md) —— 两家 bootloader 逆向综合结论
- [tuya-bootloader-reverse.md](bootloader/tuya-bootloader-reverse.md) —— 涂鸦 65 KB bootloader 逐函数逆向
- [bk-official-bootloader-reverse.md](bootloader/bk-official-bootloader-reverse.md) —— BK 官方 52 KB bootloader 逐函数逆向
- [vendor-bootloader-comparison.md](bootloader/vendor-bootloader-comparison.md) —— 两家 binary 对比

### 板端验证探针（`probe/`）
- [probe/README.md](probe/README.md) —— 最小裸探针说明（烧 @ `0x02010000`，读 core/CPUID/VTOR）
- [probe/probe.c](probe/probe.c) · [probe/probe.ld](probe/probe.ld)

### Tier-1 Bootloader 源码（`board/`）
- [board/bk7258_t5ai/bootloader/README.md](../../board/bk7258_t5ai/bootloader/README.md) —— Tier-1 bootloader 说明
- [start.S](../../board/bk7258_t5ai/bootloader/start.S) · [boot_main.c](../../board/bk7258_t5ai/bootloader/boot_main.c) ·
  [bootloader.ld](../../board/bk7258_t5ai/bootloader/bootloader.ld) ·
  [bk7236_pack_min_bootloader.py](../../board/bk7258_t5ai/bootloader/bk7236_pack_min_bootloader.py)

### NuttX 移植 worklog / prompts（`nuttx-port/`）
- [nuttx-port/git-worktree-sync-2026-07-27.md](nuttx-port/git-worktree-sync-2026-07-27.md) —— 主检出目录、clean worktree、构建链接与 PR 分支同步记录
- [nuttx-port/n7-ap-singlecore-bringup.md](nuttx-port/n7-ap-singlecore-bringup.md) —— 当前 Stage N7：物理 CPU1 独立单核 AP NuttX 启动链、双镜像与板测门禁
- [nuttx-port/cp-ap-rptun-architecture-research.md](nuttx-port/cp-ap-rptun-architecture-research.md) —— CP NuttX UP + AP NuttX SMP 双镜像、RPTUN/RPMsg、Wi-Fi/BLE 与 mailbox 复用边界的源码探索总结
- [nuttx-port/n6-bug-4295s-timer-wrap.md](nuttx-port/n6-bug-4295s-timer-wrap.md) —— 约 4295 秒后 `HF` + WDT 重启根因及修复（`CONFIG_SYSTEM_TIME64=y`；源码、ELF 与 5834.58 秒板测均已验证）
- [nuttx-port/n5-flash-filesystem.md](nuttx-port/n5-flash-filesystem.md) —— Stage N5 flash filesystem worklog（D5 raw flash r/w + D6 MTD + D7 LittleFS，board-verified 2026-07-19）
- [nuttx-port/n2-nsh-console.md](nuttx-port/n2-nsh-console.md) —— Stage N2 会话记录（boot trace、
  4 个 UART RX bug 现象/定位/修法、板端 `uname -a` 证据）
- [nuttx-port/n3-procfs-ps.md](nuttx-port/n3-procfs-ps.md) —— Stage N3 会话记录（procfs 挂载、
  `ps` / `/proc` 与 state-C 板端证据）
- [nuttx-port/n4-d0-clock-diag.md](nuttx-port/n4-d0-clock-diag.md) —— Stage N4-D0/D0D/D0F 会话记录
  （manual-reset 26 MHz baseline、loader 残留 ≈80 MHz、J-Link DWT、runtime SysTick bookkeeping、
  100Hz tick 兼容性、N4-D1 blocker）
- [nuttx-port/n5-flash-filesystem.md](nuttx-port/n5-flash-filesystem.md) —— Stage N5 flash filesystem
  （D0 layout、D1 flash ID、D2 content dump、D3 magic scan、D4 emptiness scan、D5 raw flash r/w、
  D6 MTD lower-half、D7 LittleFS；全链路 board-verified 2026-07-19）
  - **当前 Stage handoff：** [nuttx-port/n7-ap-singlecore-bringup.md](nuttx-port/n7-ap-singlecore-bringup.md)

### 参考
- [git-worktree-guide.md](git-worktree-guide.md) —— Git worktree 入门、本项目 clean worktree 与 openvela 构建工作区的关系
- [sdk-context-index.md](sdk-context-index.md) —— BK ARMINO SDK (`bk_avdk_smp`) 上下文索引

## 外部资源（不在本仓内）

| 资源 | 路径 |
|---|---|
| Beken ARMINO SDK | `$BK7258_SDK`（= `bk_avdk_smp`） |
| Tuya SDK | `$TUYA_SDK`（= `TuyaOpen`） |
| 已有 Zephyr port（含已验证最小 bootloader） | `$TUYA_SDK/zephyr-bk7258-port` |
| 涂鸦 bootloader（65 KB） | `$TUYA_SDK/zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK 官方 bootloader（52 KB） | `$BK7258_SDK/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |
