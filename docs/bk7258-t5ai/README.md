# Beken BK7258（Tuya T5-AI）openvela / NuttX 移植

把 openvela / NuttX 移植到 Beken BK7258（ARM Cortex-M33 三核、Wi-Fi 6 + BLE 5.4）Tuya T5-AI
模组。BootROM → Tier-1 bootloader → CPU0/CP NuttX、NSH、LittleFS、CPU0 IRQ/GPIO 等既有
阶段已有板端证据。当前 **Stage N7** 已完成物理 CPU1 的独立单核 AP NuttX 最小直接
启动原型、CPU0 start/stop/restart 控制、共享 SRAM/raw mailbox 协议和 LittleFS-safe
双镜像打包，状态为 `build-verified`；它不是最终 AP wrapper 架构。当前代码获准提交，
后续由用户板测；CPU1 执行、AP_READY、运行时诊断和重复启停均不得提前表述为
`board-verified`。

> 详细技术报告（评委请读这份）：**[porting-report.md](porting-report.md)**
> N2 worklog：[`nuttx-port/n2-nsh-console.md`](nuttx-port/n2-nsh-console.md)
> N3 worklog：[`nuttx-port/n3-procfs-ps.md`](nuttx-port/n3-procfs-ps.md)
> N4-D0/D0D worklog：[`nuttx-port/n4-d0-clock-diag.md`](nuttx-port/n4-d0-clock-diag.md)
> N5 flash filesystem worklog（D5 raw flash r/w + D6 MTD + D7 LittleFS，board-verified 2026-07-19）：[`nuttx-port/n5-flash-filesystem.md`](nuttx-port/n5-flash-filesystem.md)
> N7 CPU1/AP 单核启动链 worklog（build-verified，未板测）：[`nuttx-port/n7-ap-singlecore-bringup.md`](nuttx-port/n7-ap-singlecore-bringup.md)
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
| NuttX Stage N4（DPLL / 480 MHz clock bring-up） | 历史：N4-D0/D0D/D0F `board-verified`（substage）；N4-D1 blocked，整 N4 not board-verified |
| NuttX Stage N4 — D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping） | ✅ substage `board-verified`（2026-07-18，feature commit `6f596b7`，3 个 overlay 文件） |
| NuttX Stage N4 — D0F（100Hz SysTick tick-rate 兼容性） | ✅ substage `board-verified`（2026-07-18，feature commit `8dab594`，defconfig 移除 100ms override） |
| NuttX Stage N5（flash layout / ID / filesystem） | **N5-D0..D4 board-observed**（2026-07-19）；**N5-D5 raw flash r/w board-verified**（2026-07-19）；**N5-D6 MTD board-verified**（方案 A，CONFIG_BK7258_FLASH_MTD）；**N5-D7 LittleFS filesystem board-verified**（/data 挂载，probe 文件重启持久化通过）；D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`，boot/app 区不受影响） |
| NuttX Stage N6（CPU0 SDK IRQ/GPIO） | CPU0 vectors、TIMER1 IRQ 与 GPIO C0/C1/C2 已有板端验证；作为 N7 回归基线保留 |
| **NuttX Stage N7（CPU1 独立单核 AP NuttX）** | **CURRENT：最小直接启动原型 `build-verified`，未板测，非最终 wrapper 架构**；当前代码获准提交，用户板测后进入 AP wrapper 实现 |
| MTD / 文件系统 | ✅ board-verified（N5-D6 MTD + N5-D7 LittleFS，/data 挂载） |
| Tier-2 bootloader（OTA / A-B failover） | 后续，未编号 |
| 多核后续 | CPU1 单核 AP 为 N7 build-verified；物理 CPU2、AP SMP、RPTUN 与服务层均后续 |

**N7 构建产物**：`$FW/bk7258-dual/app.bin`（CP）、`app1.bin`（AP）及
`bk7258-dual-image.json`。正常更新必须按 manifest 的 boot/CP/AP 三个 physical offset-length
segment 分段写入，保留 logical `0x100000..0x1fffff` 的 LittleFS；
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
- [nuttx-port/n7-ap-singlecore-bringup.md](nuttx-port/n7-ap-singlecore-bringup.md) —— 当前 Stage N7：物理 CPU1 独立单核 AP NuttX 启动链、双镜像与板测门禁
- [nuttx-port/n6-bug-4295s-timer-wrap.md](nuttx-port/n6-bug-4295s-timer-wrap.md) —— 约 4295 秒后 `HF` + WDT 重启根因（32 位 `TICK2USEC` 溢出；源码与 ELF 已确认，尚未修复/板测）
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
- [sdk-context-index.md](sdk-context-index.md) —— BK ARMINO SDK (`bk_avdk_smp`) 上下文索引

## 外部资源（不在本仓内）

| 资源 | 路径 |
|---|---|
| Beken ARMINO SDK | `$BK7258_SDK`（= `bk_avdk_smp`） |
| Tuya SDK | `$TUYA_SDK`（= `TuyaOpen`） |
| 已有 Zephyr port（含已验证最小 bootloader） | `$TUYA_SDK/zephyr-bk7258-port` |
| 涂鸦 bootloader（65 KB） | `$TUYA_SDK/zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK 官方 bootloader（52 KB） | `$BK7258_SDK/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |
