# Beken BK7258（Tuya T5-AI）openvela / NuttX 移植

把 openvela / NuttX 移植到 Beken BK7258（ARM Cortex-M33 三核、Wi-Fi 6 + BLE 5.4）Tuya T5-AI
模组。**已完成**两家 bootloader 完整逆向 + 自制 Tier-1 bootloader + 最小探针，**板端验证**
BootROM → bootloader → app 跳转链与“启动核 = CPU0”关键事实；NuttX Stage N1、N2、N3 均已
`board-verified`（2026-07-18），Stage N4 DPLL / 480 MHz clock bring-up 为 **current / planned**。

> 详细技术报告（评委请读这份）：**[porting-report.md](porting-report.md)**
> N2 worklog：[`nuttx-port/n2-nsh-console.md`](nuttx-port/n2-nsh-console.md)
> N3 worklog：[`nuttx-port/n3-procfs-ps.md`](nuttx-port/n3-procfs-ps.md)
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
| NuttX Stage N4（DPLL / 480 MHz clock bring-up） | **CURRENT / planned**（`static-only`） |
| MTD / 文件系统 | 后续，未编号 |
| Tier-2 bootloader（OTA / A-B failover） | 后续，未编号 |
| 多核 SMP（CPU1 / CPU2） | 后续，未编号 |

**构建产物**：`$FW/all-app.bin`（= `bl_crc.bin` + `nuttx_crc.bin`，整体烧 @ physical `0x0`），
其中 `$FW = $WORKSPACE/nuttx`。console UART1 460800 8N1。

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
- [nuttx-port/n2-nsh-console.md](nuttx-port/n2-nsh-console.md) —— Stage N2 会话记录（boot trace、
  4 个 UART RX bug 现象/定位/修法、板端 `uname -a` 证据）
- [nuttx-port/n3-procfs-ps.md](nuttx-port/n3-procfs-ps.md) —— Stage N3 会话记录（procfs 挂载、
  `ps` / `/proc` 与 state-C 板端证据）
  - **当前 Stage prompt：** [nuttx-port/prompts/04-n4-clock-bringup.md](nuttx-port/prompts/04-n4-clock-bringup.md)

### 参考
- [sdk-context-index.md](sdk-context-index.md) —— BK ARMINO SDK (`bk_avdk_smp`) 上下文索引

## 外部资源（不在本仓内）

| 资源 | 路径 |
|---|---|
| Beken ARMINO SDK | `/home/lijian/project/armino/bk_avdk_smp` |
| Tuya SDK | `/home/lijian/project/TuyaOpen/TuyaOpen` |
| 已有 Zephyr port（含已验证最小 bootloader） | `/home/lijian/project/TuyaOpen/zephyr-bk7258-port` |
| 涂鸦 bootloader（65 KB） | `zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK 官方 bootloader（52 KB） | `bk_avdk_smp/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |
