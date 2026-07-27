# BK7258 N7 — CPU1 单核 AP NuttX 启动链

日期：2026-07-26

## 1. 本 Stage 边界

状态：`build-verified`（最小直接启动原型；非最终 wrapper 架构；未板测）

本 Stage 只实现：

- CPU0/CP NuttX 启动、停止和重新启动物理 CPU1；
- 物理 CPU1 运行独立 `CONFIG_SMP=n` 的 AP NuttX 镜像；
- AP 本地逻辑核编号为 0，入口向量符号为 `__vector_core0_table`；
- 共享 SRAM boot-state 与最小 mailbox doorbell；
- `app.bin` / `app1.bin` 双镜像构建和分段烧录布局。

本 Stage 不实现 CPU2、AP SMP、RPTUN/OpenAMP/RPMsg、Wi-Fi、BLE，也不改官方
`nuttx/` 源码。

## 2. 已收口的硬件事实

状态：`static-only`

- CPU1 启动控制寄存器为 `0x44010014`：bit 0 为 reset release，bit 1 为
  power-down，bit 5 为 RXEVT select，bits 8..31 为 boot address offset。
- 启动顺序固定为：`pwr_dw=0` → `rxevt_sel=1` →
  `boot_address_offset=logical_xip_addr >> 8` → `reset=1`。
- `reset=0` 表示 hold/stop；参考 SDK 的 restart 间隔为 6 ms。
- CPU1 执行 AP image 的 `__vector_core0_table`，AP 本地 logical core 0 对应
  SoC physical CPU1。`0x20000000` 是 SDK 软件 core-ID cell，不是独立硬件身份寄存器；
  本实现由 AP startup 写 local ID 0，并在共享诊断中按 AP offset 报 physical ID 1。
- raw mailbox：CPU0→CPU1 使用 MBOX0 (`0x41000000`)，CPU1→CPU0 使用 MBOX1
  (`0x41020000`)；sender 写 params 后置 ready，receiver 读 params 后置 clear。

## 3. 无重叠布局决策

状态：`static-only`

### 3.1 Flash logical/XIP layout

| 区域 | logical offset | CPU-visible address | size |
|---|---:|---:|---:|
| bootloader | `0x000000` | `0x02000000` | `0x010000` |
| CP `app.bin` | `0x010000` | `0x02010000` | `0x0f0000` |
| LittleFS | `0x100000` | SDK flash offset `0x100000` | `0x100000` |
| AP `app1.bin` | `0x200000` | `0x02200000` | `0x200000` |

CRC-packed physical offset 使用 `physical = logical * 34 / 32`（所有 partition
边界均 32-byte aligned）：CP 为 `0x11000`，LittleFS 为 `0x110000`，AP 为
`0x220000`。正常更新必须用 BKFIL/bk_loader multi-segment 分别写 boot/CP/AP，
不得用 padding 跨过 LittleFS。可选 factory image 会显式标为会擦除数据区。

### 3.2 SRAM layout

| 区域 | address | size |
|---|---:|---:|
| CP RAM | `0x28000000..0x2804ffff` | `0x50000` (320 KiB) |
| AP RAM | `0x28050000..0x2809efff` | `0x4f000` (316 KiB) |
| shared boot-state | `0x2809f000..0x2809ffff` | `0x1000` (4 KiB) |

当前 CP ELF 的 `.data + .bss` 远小于新 CP 边界；最终是否满足实际运行峰值仍需
板端 `meminfo` 回归。共享页不属于任一 image 的 `.data/.bss/heap`。

## 4. 第一版控制协议

状态：`static-only`

- CPU0 初始化共享页，写 generation/STARTING，然后按固定 SYS 顺序 release CPU1。
- AP startup 写 initial MSP/VTOR/local core ID；NuttX init task 在 heap/SysTick 已建立后
  写 AP_READY、诊断字段和 heartbeat，并通过 MBOX1 发送 READY doorbell。
- CPU0 第一版以 polling 读取 raw mailbox ready + 共享 state；暂不引入 RPTUN 或完整
  SDK mailbox channel stack。
- stop：CPU0 写 STOP command 并通过 MBOX0 doorbell 通知；AP 写 STOPPED + MBOX1 ACK
  后 park；CPU0 `reset=0`，等待 6 ms。超时则执行 forced reset hold。
- restart：stop → 6 ms → start，generation 必须递增。

## 5. 验证门禁

- `build-verified` 前：CP/AP 两套 defconfig 均 fresh build 成功；linker ASSERT、vector
  symbol、flash/RAM boundary、raw image size、CRC physical offset 和 manifest 检查全部通过。
- `board-verified` 前：必须实测 physical ID=1、VTOR/MSP/clock/SysTick/heap、CP NSH、
  Flash/LittleFS、GPIO，以及连续 start/stop/restart。
- 本会话不执行烧录；任何 build/static 结果不得表述为板端通过。

## 6. 第一版代码落地

日期：2026-07-26

状态：`static-only`

已在 team overlay 落地、尚未构建：

- `chip/include/bk7258_amp.h`：统一 flash/SRAM/共享状态和 raw mailbox 协议；
- CP linker 缩到独占 RAM/flash 边界，新增 `scripts/ld_ap.script`；
- `bk7258_ap_vectors.c` / `bk7258_ap_start.c` / `bk7258_ap_main.c`：
  `__vector_core0_table`、CPU1 UP NuttX、runtime diagnostics 和 heartbeat；
- `bk7258_ap_control.c`：CPU0 start/stop/restart、6 ms restart interval、共享状态和
  MBOX0/MBOX1 polling doorbell；
- `apctl` NSH built-in：start/stop/restart/status/cycle；
- `configs/ap/defconfig` 和 role-aware Make/CMake wiring；
- role-aware `postbuild.sh`、generic CRC expander、dual-image validator/manifest 和
  `build_dual_image.sh`；
- bootloader FAL table 改为 boot + CP + preserved data + AP，CP MSP 校验同步缩小。

## 7. AP image 首次构建

日期：2026-07-26

状态：`build-verified`（仅 AP 编译/链接/打包；未板测）

- Python packaging scripts `py_compile` PASS；shell scripts `bash -n` PASS；
  `git diff --check` PASS。
- AP fresh configure/build 首次被 `bk7258_bringup.c` 无条件引用
  `CONFIG_NSH_PROC_MOUNTPOINT` 阻塞；AP 不启用 NSH/procfs，因此给 procfs mount 增加
  `CONFIG_FS_PROCFS && CONFIG_NSH_PROC_MOUNTPOINT` 编译门禁后重建成功。
- AP ELF：text `43092` B，data `816` B，bss `7080` B。
- `app1.bin`=`43908` B，`app1_crc.bin`=`46682` B；目标 physical offset
  `0x00220000`。
- symbol gates：`__vector_core0_table == _vectors == 0x02200000`；
  `__start=0x02200184`；`bk7258_ap_main=0x02200294`；RAM vectors
  `0x28050800`；`_eheap=0x2809effc`。
- generated config：`CONFIG_BK7258_AP_CORE=y`、`CONFIG_SMP_NCPUS=1`、
  `CONFIG_INIT_ENTRYPOINT="bk7258_ap_main"`；没有启用 CP WDT/Flash 功能。

## 8. CP image 首次构建

日期：2026-07-26

状态：`build-verified`（CP 编译/链接/打包；未板测）

- fresh CP configure/build exit 0。
- CP ELF：text `157238` B，data `5724` B，bss `10072` B；`_eheap=0x2804fffc`，
  证明 linker 已限制在 CPU0-owned SRAM。
- `app.bin`=`162964` B，`app_crc.bin`=`173162` B，legacy CP-only
  `all-app.bin`=`242794` B；CP physical segment 为 `0x11000 + 0x2a46a`，远低于
  LittleFS physical boundary `0x110000`。
- final ELF 含 `bk7258_ap_control_initialize/start/stop/restart/get_status`；
  built-in registry/strings 含 `apctl`、usage 和 AP status 输出。
- generated config 保留 `BK7258_FLASH_MTD/LITTLEFS`、SDK IRQ bridge/timer test，并启用
  `CONFIG_BK7258_AP_CONTROL=y`；`AP_AUTOSTART` 保持关闭，避免未刷入 app1 时自动释放 CPU1。
- build log 中的 SDK header `-Wundef` 为既有 warning；本轮 AP control/CLI 源没有 compiler
  error。CPU0 NSH、Flash、GPIO 是否仍正常只能由后续板测证明。

## 9. 最终 dual-image rebuild 与 focused review 收口

日期：2026-07-27

状态：`build-verified`（未板测）

最终 review 在首轮 dual build 后确认并修复两个 blocker：

1. `BK7258_AP_CORE` 原先未拉入 `TIMER` / `TIMER_ARCH` / `ARMV8M_SYSTICK`。
   旧 AP ELF 的 `up_timer_initialize()` 只写 SysTick RELOAD 后直接返回，AP runtime
   self-check 必然得到 `BAD_SYSTICK`。现由 AP role Kconfig 强制 select 三项；最终 ELF
   明确包含 `systick_initialize`、`up_timer_set_lowerhalf`，反汇编也确认两次调用存在。
2. start/stop timeout 后共享 state 可能停在 `STARTING` / `READY`，下一次 start 会永久
   `-EBUSY`。现新增 `BK7258_AP_ERROR_TIMEOUT`：start 失败完成 reset/power-down 后落到
   `FAILED` 并清 command；forced stop 最终落到 `STOPPED`，因此下一 generation 可重试。

随后 packaging review 确认 root build tree 曾混用 first-CP snapshot 与 final CP restore：manifest
和 root `app_crc.bin` 指向 first CP，而 `nuttx_crc.bin` / CP-only `all-app.bin` 来自 final CP。
`build_dual_image.sh` 现以最终 restored CP 为唯一权威 snapshot，并 fail-closed 比较 root
`app.bin`、`app_crc.bin`、`nuttx_crc.bin`、`all-app.bin` 与 dual package，消除该歧义。

最终完整构建命令：

```text
board/bk7258_t5ai/scripts/build_dual_image.sh
```

exit 0，日志：`/tmp/bk7258-dual-build-final-v3.log`。builder 完成 bootloader → CP → AP →
CP restore，最终 `$WORKSPACE/nuttx/.config` 为 CP role：`CONFIG_BK7258_AP_CONTROL=y`、
`CONFIG_BK7258_AP_CORE` unset、`CONFIG_UP=y`、`CONFIG_NCPUS=1`。dual update 的权威产物仍是
`$WORKSPACE/nuttx/bk7258-dual/`；同时 root CP compatibility artifacts 已由 builder gate 证明
与 manifest CP 完全一致，root `all-app.bin` 明确仍是 bootloader + CP 的 CP-only image。

### 9.1 最终 ELF gates

- CP ELF：text `157358` B，data `5724` B，bss `10072` B；`_eheap=0x2804fffc`。
- AP ELF：text `46044` B，data `836` B，bss `7128` B。
- AP symbols：
  - `__vector_core0_table == _vectors == 0x02200000`
  - `__start=0x02200184`
  - `bk7258_ap_main=0x02200294`
  - `_sram_vectors=0x28050800`
  - `_eheap=0x2809effc`
  - `up_timer_set_lowerhalf=0x02203910`
  - `up_timer_initialize=0x022060e4`
  - `systick_initialize=0x022063a0`

### 9.2 最终 artifacts

| 文件 | bytes | SHA-256 |
|---|---:|---|
| `app.bin` | `163084` | `965195af1f733d372232319cabf1650409ce1ae94655e198d0f1d41d2dc2e97a` |
| `app_crc.bin` | `173298` | `a26fa36f1324f2d000a3eb35a47e9be6926a62c91acfab6cfac7e23a4beb3f95` |
| `app1.bin` | `46880` | `061db0e9cc0c71e3d2272faefc6a1c18d4e207b7398fef130d81e20879cda90d` |
| `app1_crc.bin` | `49810` | `48eabdd6c5ea5929b1335630c829f7a4c3ccf40dac526dd1ad9aaef690641e97` |
| `bl_crc.bin` | `69632` | `507400e1f86769d064ad7206814129dd60b8dcc6e483e7ed12440c89fd747f56` |
| root CP-only `all-app.bin` | `242930` | `d3c15eef234c709a1e09d5e493da52648e411dfe0cc5ef13ce692e031f9a94f4` |
| `all-app-factory.bin` | `2278034` | `ff201666fecabab0a4c817c217a02d2009899d8ea796194acadab713c063b72a` |

normal split-update arguments：

```text
bl_crc.bin@0x0-0x11000
app_crc.bin@0x11000-0x2a4f2
app1_crc.bin@0x220000-0xc292
```

CP physical end `0x3b4f2` 低于 LittleFS physical start `0x110000`；AP 从 LittleFS
physical end `0x220000` 开始。manifest gate PASS：normal split update 保留 LittleFS；
`all-app-factory.bin` 以 `0xff` 跨过数据区，明确不保留 LittleFS。

### 9.3 最终静态检查

- `git diff --check` PASS。
- Stage N7 Python packers `py_compile` PASS。
- `postbuild.sh` / `build_dual_image.sh` `bash -n` PASS。
- focused checkpatch 只有 6 个 team-overlay file-header path false positive；过滤该已知
  规则后无其它 finding。
- 最终 AP timer object gate PASS：`up_timer_initialize()` 调用
  `systick_initialize(true, cpu_hz, -1)` 和 `up_timer_set_lowerhalf()`。
- AP map SDK-isolation gate PASS：未链接 CP `bk_idk/armino_as_lib` / `libdriver.a`。
- root artifact consistency gate PASS：dual/root `app.bin` 与 `app_crc.bin` 一致，
  `app_crc.bin == nuttx_crc.bin`，且 root `all-app.bin == bl_crc.bin + app_crc.bin`。
- 两轮 focused review 的确认 blocker 均已修复；修复后复审未发现本 Stage 范围内新的
  source-level blocker。

### 9.4 仍仅能由板端证明的门禁

尚未执行烧录，也未声称板端通过。下一最小步骤是在用户明确授权后按 manifest 分段烧入
boot/CP/AP，保留现有 LittleFS，然后依次验证：

1. CPU0 NSH、Flash/LittleFS 和 GPIO baseline 回归；
2. `apctl start` 后 physical CPU1 进入 AP image 并报告 READY；
3. local/physical ID、initial/runtime VTOR、MSP、clock、SysTick、heap 和 heartbeat；
4. `apctl stop` / `restart` / `cycle` 多轮可重复；
5. 缺失或损坏 `app1` 时 timeout 后能够再次 start，不再被 stale `STARTING` 卡死。

`CONFIG_BK7258_AP_AUTOSTART` 继续关闭。在以上板测完成前，不进入 CPU2、AP SMP、RPTUN、
Wi-Fi 或 BLE。

## 10. 当前原型提交与 wrapper 下一阶段决策

日期：2026-07-27

状态：`build-verified`（未板测）

用户确认当前 N7 最小直接启动原型可以提交，板端分段烧录和 CPU1 运行验证由用户后续完成。
当前实现不是原定 wrapper 架构的完成态：AP 未编译/链接 Beken AP SDK libraries，raw mailbox
等路径仍使用 team-owned direct MMIO；只有 CPU0 CPU1 control 边界包装了四个 CP SDK API。

板端验证完成后，下一阶段回到原定 wrapper 方向：

```text
NuttX -> team-owned AP wrapper -> AP SDK/HAL libraries -> BK7258 hardware
```

除 linker、vector table、reset entry 等启动前必须直接实现的部分外，AP clock、mailbox、core
identity、IRQ/timer 和后续服务应通过 wrapper 隔离。用户明确要求 wrapper 实现阶段不调用
skills，也不使用任何 subagent；由主模型直接完成分析、实现和验证。
