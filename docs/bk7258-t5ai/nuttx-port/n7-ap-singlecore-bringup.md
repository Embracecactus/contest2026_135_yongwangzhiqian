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
- `configs/ap_up/defconfig` 和 role-aware Make/CMake wiring；
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

## 11. clean PR 分支同步与 AP SDK 静态库集成

日期：2026-07-27

状态：`build-verified`（未板测）

本节覆盖 §9.4 和 §10 中“AP 不链接 SDK libraries”及“`AP_AUTOSTART` 关闭”的旧状态。同步后的
PR 分支先迁移 CPU1 AP 前置实现，再加入按 CP/AP role 分离的 SDK headers、config 和静态库：

- AP 前置提交：`a0271af`（由旧分支 `38699e8` 迁移并与现有 GPIO/N6 状态合并）；
- SDK role 增量：`b07f949`（由旧分支 `6b2d98c` 迁移）；
- CP 使用 `bk_idk/armino_as_lib/cp/`，AP 使用 `bk_idk/armino_as_lib/ap/`；
- AP 仍由 NuttX 持有 reset entry、调度、堆和同步语义，只链接选定的 AP driver/HAL/PM/common
  archives；这不是最终 AP wrapper 收口；
- 当前 CP defconfig 为 `CONFIG_BK7258_AP_CONTROL=y`、`CONFIG_BK7258_AP_AUTOSTART=y`。

完整构建命令：

```text
board/bk7258_t5ai/scripts/build_dual_image.sh
```

构建 exit 0，日志：`/tmp/bk7258-dual-build-sync-2026-07-27.log`。CP → AP → CP restore 和
root/manifest artifact consistency gates 全部通过。最终角色隔离检查：CP map 只引用
`armino_as_lib/cp`，AP map 只引用 `armino_as_lib/ap`，没有跨角色 SDK 路径。

| 文件 | bytes | SHA-256 |
|---|---:|---|
| `app.bin` | `171956` | `ab36a3b388898fb674eed78f1bcf51a14150ad7423b311fe7d9c62cd71838b29` |
| `app_crc.bin` | `182716` | `21d64c7f9a21d367606ab3fd2176c3f9183e71c2b6009820320dbd4be0dbdce0` |
| `app1.bin` | `64346` | `73d4842ca28a5822326d87b50a7d857962bda73b8a7618cbba04334bfc20da58` |
| `app1_crc.bin` | `68374` | `aa45d0bc54f478f512fa25274f7dbce8e5a30a5c01c7666841d279e8fd0af7ba` |
| root CP-only `all-app.bin` | `252348` | `28de7c4a826493b6a6bf5f4d272e799784e0bc229147b73d03078e1fadc80d46` |
| `all-app-factory.bin` | `2296598` | `9b2ae317fc7a77349a2306f5cfae36e3981d671a93fc508168f511fd8a73c1c6` |

normal split-update arguments：

```text
bl_crc.bin@0x0-0x11000
app_crc.bin@0x11000-0x2c9bc
app1_crc.bin@0x220000-0x10b16
```

本轮没有烧录或板测。由于 `AP_AUTOSTART=y`，板测时必须确保 CP 与 AP segment 成套烧入；仍需
验证 CPU0 基线、CPU1 READY/runtime 状态以及 stop/restart/cycle。上述结果只能称为
`build-verified`，不得称为 `board-verified`。

## 12. AP 早期 HardFault/NMI 现场捕获

日期：2026-07-27

状态：`build-verified`（故障现场来自用户提供的板端 dump；本轮未烧录）

用户提供的共享页 dump 已证明 CPU1 从 `0x02200000` 取到正确向量，`__start` 在 XIP 上真实执行，
初始 MSP 落在 AP RAM 顶部，且共享页未被 linker/heap 越界覆盖。`runtime_*` 仍全零，而旧
fault vector 会写 `BAD_BOOT_STATE + FAILED`，因此当前阻塞点收敛为 `__start -> nx_start` 早期的
NMI/HardFault，而不是 flash/SRAM 布局。

本轮将 fault vector 改为可直接定位的现场记录：

- 保持原 `bk7258_ap_boot_state_s` 严格为 `0x80` 字节，原 ABI 和字段偏移不变；
- 在 error enum 尾部追加 `NMI=7`、`HARDFAULT=8`，不改变旧错误码数值；
- `reserved[0..3]`（`0x2809f070..0x2809f07c`）快速记录
  `IPSR / HFSR / CFSR / stacked PC`；
- 共享页 `0x2809f080` 起新增 80-byte `AFLT` v1 扩展，记录 generation、exception、error、
  `EXC_RETURN`、原始异常 SP、HFSR/CFSR/MMFAR/BFAR，以及基本异常帧
  `r0-r3/r12/lr/pc/xpsr`；magic 最后发布，避免半写记录被误判有效；
- naked vector entry 在任何 C 序言改写 LR/SP 前捕获 MSP/PSP、EXC_RETURN 和 IPSR；同时兼容
  basic frame 与可选 FP extended frame，并在解引用前检查 frame 位于 AP RAM；异常帧通过
  volatile scalar loads 读取，避免 4-byte-aligned frame 被编译器合并成可能再次 fault 的 `LDRD`；
- CP 每次创建新 generation 时先清 fault magic，避免把上一轮现场误认为本轮故障。

完整 dual-image rebuild exit 0，日志：
`/tmp/bk7258-n7-fault-capture-build-v3.log`。最终 manifest 仍通过 CP/root 一致性和 LittleFS
保留门禁；本轮 segment 为：

```text
app_crc.bin@0x11000-0x2c9bc
app1_crc.bin@0x220000-0x10c48
```

AP ELF 静态门禁：`__vector_core0_table == _vectors == 0x02200000`；vector slot 2/3 均为
`bk7258_ap_fault_entry|1 == 0x02200141`；entry 反汇编确认依次执行
`TST EXC_RETURN bit2 -> MRS MSP/PSP -> MOV LR -> MRS IPSR -> branch handler`。

下一板端最小动作是成套更新 CP/AP segment，触发一次 AP start，然后读取：

```text
mem32 0x2809f070 4
mem32 0x2809f080 20
```

扩展记录中的 `stacked_pc` 位于 `0x2809f0c8`，`stacked_lr` 位于 `0x2809f0c4`；用本轮
`nuttx/bk7258-dual/nuttx-ap.elf` 执行 `arm-none-eabi-addr2line -f -C -e <elf> <pc>`，即可把
故障收敛到具体函数/源码行。拿到 PC、CFSR、BFAR/MMFAR 后再做根因修复；在此之前不猜测性
禁用 UART、timer 或 RAM vector。

## 13. AP autostart 等待触发整机 WDT 重启环

日期：2026-07-27

状态：`build-verified`（重启日志来自用户板端；修复尚未板测）

更新 fault-capture 镜像后，板端在 AP/SDK 打印 GPIO0 busy 提示后反复回到
`HFu_bootloader enter`。这表示发生的是 CPU0/整 SoC reset，而不是 CPU1 单独 reset；GPIO0 提示
只是 reset 前最后一条可见日志，不能据此认定 GPIO unmap 是复位源。

源码顺序确认了直接原因：`board_app_initialize()` 原先先执行 `bk7258_ap_start()`，其失败路径可
polling 等待最长 3000 ms；停止 bootloader AON WDT 的 `bk7258_wdt_initialize()` 却排在 AP
等待之后。若 AP 新 fault handler 没能送达 doorbell，CPU0 会在等待期间被仍运行的 bootloader
AON WDT 拉回整机复位，于是每轮都重新进入 bootloader，且来不及保留一个可交互的 CP NSH。

修复已将 `bk7258_wdt_initialize()` 移到 AP control/autostart 之前。它先完成 SDK WDT/timer 状态
接管并调用 `bk_aon_wdt_stop()`，随后才允许最长 3 秒的 AP start 等待。完整 dual build exit 0，
日志：`/tmp/bk7258-n7-wdt-before-ap-build.log`。CP ELF 反汇编确认调用顺序为：

```text
0x020319c0  bk7258_wdt_initialize
0x020319c4  bk7258_ap_control_initialize
0x020319d0  bk7258_ap_start
```

`bk7258_wdt_initialize()` 反汇编同时确认调用 `bk_aon_wdt_stop()`。normal split-update 参数保持：

```text
app_crc.bin@0x11000-0x2c9bc
app1_crc.bin@0x220000-0x10c48
```

本次 AP image 未变化，因此板端可只更新新的 CP `app_crc.bin` 来解除重启环；为了避免产物代际
混淆，完整验证仍建议 CP/AP 使用同一 `nuttx/bk7258-dual/` 目录。预期修复后即使 AP 仍失败，
CPU0 也应在最多 3 秒后继续进入 NSH，并保留共享 fault record 供 `mem32` 读取。

## 14. 首次完整 fault record：AP reset stack 契约缺失（非最终根因）

日期：2026-07-28

状态：`board-verified`（stack 迁移生效；同一 SVC fault 仍存在，最终根因见 §15）

用户读取到的第一份完整 `AFLT` 记录为：exception `3`、error `8`、
`EXC_RETURN=0xfffffff1`、HFSR `0x40000000`、CFSR `0x01000000`、stacked
PC `0x022005f2`、LR `0x02200675`、xPSR `0x6900000b`。解析结果：

- HFSR.FORCED=1，CFSR.UFSR.UNALIGNED=1；BFAR/MMFAR valid bits 未置位，因此记录中的
  `0xe000ed34/0xe000ed38` 不是有效 fault address；
- PC 落在 `exception_common` 的 `ldmia.w r0!, {...}` 上，LR 落在 `arm_doirq()` 返回点；
- fault frame 的 r0 为 `0xaaaaaaaa`，即异常返回路径拿到了未初始化的 context pointer；
- xPSR.IPSR=11，证明 fault 发生在第一次 SVC context switch 的恢复阶段，而非 GPIO/UART/timer
  MMIO 访问。

同时确认了一项独立的启动栈缺陷：team-owned AP vector 绕过了 NuttX common ARM reset wrapper，
slot 0 使用 AP RAM/heap 顶 `0x2809effc`，slot 1 直接进入 `__start`，从未执行
`arm_initialize_stack()`。这会让 Thread stack 落入 allocator 管理区，并让专用 interrupt stack
闲置，必须修复；但后续板测证明它不是 `0xaaaaaaaa` context 的最终来源。

修复现严格复用 NuttX common ARM 启动契约：

- vector slot 0 改为 `IDLE_STACK = _ebss + CONFIG_IDLETHREAD_STACKSIZE = 0x280537d8`；
- vector slot 1 改为 team-owned reset wrapper；
- wrapper 先调用 `arm_initialize_stack()`：保留 reset/IDLE stack 为 PSP，设置
  `MSP=0x28050800`（dedicated interrupt-stack top），再清 LR 并跳转 `__start`；
- 原 `BK7258_AP_INITIAL_MSP` 重命名为 `BK7258_AP_HEAP_END`，避免继续把 heap end 当作 reset MSP。

完整 dual build exit 0，日志：`/tmp/bk7258-n7-ap-standard-stack-build.log`。反汇编和 vector gate：

```text
vector[0] = 0x280537d8
vector[1] = 0x02200159  (bk7258_ap_reset_entry | 1)
vector[2] = 0x02200141  (fault entry | 1)
vector[3] = 0x02200141  (fault entry | 1)
arm_initialize_stack: PSP <- reset stack; CONTROL.SPSEL <- 1; MSP <- 0x28050800
```

新 AP 分段为：

```text
app1_crc.bin@0x220000-0x10cae
SHA-256: 2037f97a1cfa0599edfd662f5d4097e1b76457d17c97082f290cf9a1d6c30728
```

解除 WDT 重启环的 CP 已在上一轮板测生效。更新该 AP segment 后，板端确认
`initial_msp=0x28050800`、fault SP=`0x280507e0`，证明 PSP/MSP/interrupt-stack 迁移生效；但同一
SVC restore 仍读到 `0xaaaaaaaa`，因此继续按 §15 追踪阻塞调用者。

## 15. 最终 SVC 根因：SDK early delay 阻塞唯一 IDLE task

日期：2026-07-28

状态：`build-verified`（修复待板测）

第二轮 fault record 仍为 HFSR.FORCED + CFSR.UNALIGNED，PC 位于新的
`exception_common` restore 指令 `0x02200606`，但 stack 已正确迁移到 interrupt stack。用户补充的
原始 SVC frame 和 scheduler globals 为：

```text
SVC R0(command) = 2 (SYS_switch_context)
SVC R1(old TCB) = 0x28051820 (IDLE TCB)
SVC PC          = 0x022010a2 (nxsched_switch after svc)
g_readytorun    = NULL
g_running_tasks = NULL
```

调用点 LR `0x02201d3d` 落在 `nxsig_clockwait()`。其源码先执行
`nxsched_remove_self(IDLE)`，ready list 因没有其它线程而变空，再以 new TCB=NULL 发起 SVC；
`arm_doirq()` 最终从地址 `0x000000ac` 读到板端填充值 `0xaaaaaaaa`，返回后由 `LDMIA` 触发
UNALIGNED fault。因此这不是 TCB 初始化失败，而是 **NuttX 启动阶段唯一的 IDLE task 被错误阻塞**。

AP ELF 的唯一 early SDK delay 调用链已静态闭合：

```text
up_initialize / serial init
 -> uart_clock_enable
 -> sys_drv_dev_clk_pwr_up
 -> bk_pm_clock_ctrl
 -> pm_cp1_mailbox_send_data
 -> rtos_delay_milliseconds(2)
 -> nxsig_usleep
 -> nxsig_clockwait
 -> remove IDLE, ready list empty
```

`rtos_delay_milliseconds()` 来自 team-owned `bk7258_os_adapt.c`，原实现无条件映射到
`nxsig_usleep()`，且 `rtos_is_scheduler_started()` 错误地恒返回 true。修复为：

- OS 尚未进入 IDLE loop、当前 task 为 IDLE、或处于 interrupt context 时，SDK delay 使用
  `up_mdelay()` busy wait，绝不阻塞调度器；
- 正常多任务运行阶段仍使用 `nxsig_usleep()`，保留可调度的延时语义；
- `rtos_thread_msleep()` 共用同一 early-safe helper；
- `rtos_is_scheduler_started()` 改为真实返回 `OSINIT_IDLELOOP()`。

完整 dual build exit 0，日志：`/tmp/bk7258-n7-ap-early-delay-build.log`。反汇编确认 initstate
`<= OSINIT_IDLELOOP` 时直接 branch `up_mdelay`，只有正常线程态才可进入 `nxsig_usleep`。新分段：

```text
app_crc.bin@0x11000-0x2c9de
app1_crc.bin@0x220000-0x10d14
app1_crc.bin SHA-256: bbd8da7980717e427c8ced96a6249a4cf8aeb69e99de95e7d9c8f275f93506d4
```

由于 OS adaptation 同时参与 CP/AP role 构建，下一轮应成套更新本目录中的 CP/AP segment。成功门禁
仍是 AP state READY、error 0、RAM VTOR/interrupt MSP 合法并开始递增 heartbeat。

## 16. AP 串口资源越界与 CPU0 fault 可观测性收口

日期：2026-07-28

状态：`build-verified`（headless AP 与 CPU0 fault recorder 待板测）

early-safe delay image 已越过上一轮首个 SVC fault，但板端随后出现：

```text
[AP]
Mailbox send data fail[ret:-4103]
[hal]
gpio: 0 is used.Please confirm unmap isn't impact is working module.!
HF
HFu_bootloader enter
```

静态归属已经闭合，但旧 `HF` 仍不能区分 exception 2/3：

- `[AP] Mailbox send data fail` 来自 AP SDK `pwr_clk.c` 的 `TAG "AP"`；调用发生在 AP NuttX
  serial bring-up 通过 `sys_drv_dev_clk_pwr_up()` 请求 UART clock 时，`MB_CHNL_PWC` 尚未 open；
  `mb_chnl_write()` 看到 logical channel `in_used == 0` 后返回 `BK_ERR_STATE` (`-4103`)；
- GPIO0 warning 同属 AP UART pin mapping，证明独立 AP image 正在争用 CP console 所有的 UART/GPIO；
- 字符串 `HF` 来自 CPU0 team-owned `bk7258_hardfault_handler()`，该入口同时挂在 CPU0 vector
  slot 2/3；AP ELF 只有 `bk7258_ap_fault_entry()`，不会打印 `HF`。因此它能证明 CPU0 进入
  NMI/HardFault 入口，但不能仅凭旧日志断言是 NMI 还是 HardFault。

当前阶段 AP 不需要本地 console；其健康度由共享 state、doorbell 与 heartbeat 上报。修复将 AP 配置改为
headless：

```text
# CONFIG_DEV_CONSOLE is not set
# CONFIG_SERIAL is not set
```

最终 AP ELF 已不再包含活动的 `arm_serialinit`、`bk_uart_driver_init` 或
`pm_cp1_mailbox_send_data` symbol，因而启动阶段不再触发该 SDK UART/PM-mailbox/GPIO0 路径。

CPU0 诊断同时升级：

- `__start()` 在 data/BSS/serial 初始化前直接向 `0x44000600` 写入 `0x005a0000`、
  `0x00a50000`，提前关闭 bootloader AON WDT；
- CPU0 NMI/HardFault 使用 naked entry 按 EXC_RETURN 选择 MSP/PSP；完整 frame 记录写入
  `0x2809f100`，magic 为 `CFLT` (`0x544c4643`)；
- 入口先尝试关闭 AON WDT 与 NuttX automonitor 使用的 main WDT，目标是避免 parked fault
  再形成重启环；§18 板测证明该动作不能保证阻止所有 reset source；
- UART fault line 现在输出 `E/X/S/H/C/P/L/Q`，可直接区分 exception、EXC_RETURN、SP、
  HFSR、CFSR、stacked PC/LR/xPSR；frame 读取反汇编仅含标量 `LDR`，无 `LDRD`。

完整 dual build exit 0，日志：`/tmp/bk7258-n7-headless-ap-cp-fault-build.log`。AP vector 与产物为：

```text
vector[0] = 0x28052488
vector[1] = 0x02200159
vector[2] = 0x02200141
vector[3] = 0x02200141

app_crc.bin@0x11000-0x2cbdc
SHA-256: 09f8d3cff75dadf9e7c340cbbeb13bd9a3da2a73205009db5df62ed3465655e2

app1_crc.bin@0x220000-0xac86
SHA-256: 270335090d1efee89de9f556dfb8f50bd574da9b1388055b7df51d1e9bbe7f51
```

本轮必须成套更新 CP/AP segment。预期正常路径不再出现 AP PM-mailbox failure 和第二个 GPIO0 warning，
随后 `apctl status` 应进入 READY；若 CPU0 仍进入 fault，UART 应输出完整诊断并写入共享记录：

```text
mem32 0x2809f100, 20
```

板端 READY/heartbeat 或新 `CFLT` 内容仍待实机证据，当前不标记为 board-verified。

## 17. CPU0 reset stack 与 AP 对齐

日期：2026-07-28

状态：`build-verified`（CPU0 PSP/MSP 迁移待板测）

在为 CPU0 增加 fault recorder 后继续检查 reset contract，确认 CP 原先与修复前 AP 存在同类隐患：

- vector slot 0 使用 `0x2804fffc`（CP RAM/heap end）；
- slot 1 直接进入 `__start()`；
- 整个启动路径没有其它 `arm_initialize_stack()` 调用；
- 因此 Thread/IDLE 与 handler 都继续使用 MSP，专用 2 KiB interrupt stack 未被安装，同时 reset
  stack 位于 allocator 管理区顶端。

该缺陷不是对上一份 `HF` 的新增因果断言，但属于独立且必须消除的 NuttX ARM 启动契约错误。修复与
已板测生效的 AP wrapper 保持一致：

- slot 0 改为 `_ebss + CONFIG_IDLETHREAD_STACKSIZE`；
- slot 1 改为 `bk7258_reset_entry()`；
- wrapper 调用 `arm_initialize_stack()`，将 reset/IDLE stack 保留为 PSP，并设置
  `MSP=0x28000800`；
- 原误导性的 `BK7258_CP_INITIAL_MSP` 重命名为 `BK7258_CP_HEAP_END`。

最终 CP ELF 静态门禁：

```text
vector[0] = 0x2800476c  (_ebss 0x28003f6c + 0x800)
vector[1] = 0x0201019d  (bk7258_reset_entry | 1)
vector[2] = 0x02010185  (CPU0 fault entry | 1)
vector[3] = 0x02010185  (CPU0 fault entry | 1)

arm_initialize_stack:
  PSP <- reset stack
  CONTROL.SPSEL <- 1
  MSP <- 0x28000800

vector[64..65] = 0x32374b42 0x00003633 ("BK7236\\0\\0")
```

完整 dual build exit 0，日志：`/tmp/bk7258-n7-cp-standard-stack-build.log`。WSL 文件时间存在
约 0.65 秒偏移，因此 GNU make 输出 clock-skew warning；日志无 compile/link error，且最终 ELF
vector、wrapper 与 `arm_initialize_stack()` 反汇编均确认来自本轮源码。新 normal split segment：

```text
app_crc.bin@0x11000-0x2cc42
SHA-256: 06c2b790c73698049f68636568638d24e72525e2fcb61b2dfefa0d6b91783d8e

app1_crc.bin@0x220000-0xac86
SHA-256: 270335090d1efee89de9f556dfb8f50bd574da9b1388055b7df51d1e9bbe7f51
```

> 注：以上 SHA-256 是该次构建的历史产物标识，不是跨构建可复现门禁。CP 镜像包含
> `CONFIG_VERSION_BUILD + __DATE__ + __TIME__`，`/proc/version` 还会编入配置目录路径；时间或
> `configs/` 目录名变化都会改变 CP raw/CRC hash。AP headless 镜像当前不包含这条路径，hash 可保持稳定。

本轮板测应使用新的 CP segment 与 §16 的同代 headless AP segment。CPU0 正常启动后，异常上下文
应使用 `0x28000000..0x280007ff` dedicated interrupt stack；AP 成功门禁仍为 READY、error 0 与
递增 heartbeat。

## 18. N7 AP 单核 READY 板端闭环与一次性 CPU0 task-exit fault

日期：2026-07-28

状态：`board-verified`（AP READY/heartbeat 与 CP PSP 迁移通过；一次性 CPU0 exit fault 保留观察）

用户使用 §17 的 CP 与 headless AP 产物进行了两次完整下载。第二次下载后的连续板测稳定通过：

```text
AP state=READY(2) error=0 generation=1
AP core local=0 physical=1
VTOR(init/run)=02200000/28050800
MSP(init/run)=28050800/28050800
clock=320000000
SysTick ctrl/load=00000007/0030d3ff
heap=28052488..2809effc
heap test=280536e8
doorbells cp/ap=0/1
heartbeat: 304 -> 343 -> 377 -> 414 -> 455 -> 502
```

这完成了 N7 AP 单核 bring-up 的主要板端门禁：

- physical CPU1 独立运行 AP-local UP NuttX；
- flash VTOR `0x02200000` 正常迁移到 AP RAM vector `0x28050800`；
- dedicated interrupt MSP 为 `0x28050800`；
- 320 MHz 与 SysTick 正常；
- heap 边界和实际 allocation test 合法；
- AP READY doorbell 已送达，heartbeat 持续递增；
- headless AP 后不再出现 `Mailbox send data fail[ret:-4103]` 或 AP GPIO0 二次争用。

第一次完整下载时，AP 已 READY 且 heartbeat 从 337 增至 457，但第二个 `apctl status` task 退出时
CPU0 捕获到一次异常：

```text
HF E=00000003 X=fffffff5 S=28007158
   H=40000000 C=00000001
   P=0201ab6c L=02015b99 Q=21000000
```

当前 ELF 映射与解析：

- `E=3`：CPU0 HardFault；
- `HFSR=0x40000000`：FORCED；
- `CFSR=0x00000001`：MemManage IACCVIOL；
- PC `0x0201ab6c`：`up_exit()` 中 `arm_fullcontextrestore()` 的 `svc 0` 后一条指令；
- LR `0x02015b99`：`nxsched_release_tcb()` 尾部；
- SP `0x28007158` 位于普通 task stack，结合 EXC_RETURN 证明 CP Thread mode 已使用 PSP，§17
  的 reset-stack 迁移在板端生效；
- fault 发生在 `apctl` builtin task 的 exit/context-restore 边界，不是 AP fault，也不否定 AP READY。

异常输出后系统仍重新进入 bootloader，说明 CPU0 fault handler 中的 WDT stop 尚未在该路径上阻止
最终 reset；reset source 可能不是仍可停止的 watchdog，不能继续按“handler 必然停驻”描述。
第二次完整下载后相同 `apctl status` 连续执行至少六次均稳定，因此当前证据更符合一次性下载/复位
状态或低概率 task-exit restore 异常，而不是可稳定复现的 AP bring-up 缺陷。

重启后的：

```text
[ipc_svr]
create_socket failed.
```

不影响 AP READY、doorbell 或 heartbeat，单独归入后续 CP IPC service 整理，不作为 N7 阻塞项。
当前结论：**N7 AP 单核独立 NuttX bring-up 已板端通过**；CPU0 首次下载后的单次 task-exit fault
在本阶段当时先保留观察。

> **2026-07-29 更新：**该 CPU0 fault 后续已稳定复现、通过 UART/J-Link 收敛到
> `arm_doirq()` NULL context restore 与非 HIPRI IRQ 嵌套，并在清除所有无关实验后以四文件
> team-overlay 最小修复重新板测通过。完整复盘见
> [`n7-bug-cpu0-task-exit-hardfault.md`](n7-bug-cpu0-task-exit-hardfault.md)。

## 19. 进入 AP-SMP 前的配置目录归一化

日期：2026-07-28

本轮只做配置路径结构重构，未移动 `chip/` 源文件、未修改 Kconfig role symbol、未开始 CPU2/SMP：

```text
configs/nsh  -> configs/cp_nsh
configs/ap   -> configs/ap_up
```

`build_dual_image.sh` 与 BK7258 文档中的当前构建路径已同步更新；历史 `/proc/version` 原始输出仍
保留当时的 `configs/nsh` 字符串，并在对应 worklog 中注明现名。两个 `defconfig` 内容未变化：

```text
cp_nsh/defconfig  7cb6ebf5511c421904116d0c54b98203064a2d3fe03eb31472015da0647d7e75
ap_up/defconfig   8978f9261cdd485deb43368e86eed9388db7278a829b37999103ae70392d3529
```

旧 build tree 的 `nuttx/Make.defs` 仍指向已删除的 `configs/nsh`，第一次验证因此在 distclean
阶段失败；清除旧 apps/NuttX 生成状态后，完整 `build_dual_image.sh` 重新执行成功（CP → AP-UP →
恢复 CP），最终 manifest、CP-only `all-app.bin` 和 factory image 校验全部通过。

AP 产物与重命名前逐字节一致：

```text
app1.bin       4068a5d5736b6a6ebaf212746bae0f51d72ded0e55a97e38b2671dd20472524e
app1_crc.bin   270335090d1efee89de9f556dfb8f50bd574da9b1388055b7df51d1e9bbe7f51
nuttx-ap.elf   5a6530dee88748caeb35077cffaad3d7e225a8d0d507edf0d526300293f079ac
```

CP raw/CRC hash 确实变化；原因不是执行逻辑，而是配置路径从 `nsh` 增长为 `cp_nsh`，同时编译时间戳变化，使
`app.bin` 从 172556 B 增至 172560 B；CRC 物理长度仍为 183362 B。不能将历史 CP hash
`06c2...` 或本轮重命名前 hash `267f...` 用作跨构建等价门禁。结构门禁结果为：

- `.vectors` 320 B 逐字节一致，SHA-256 均为
  `be73600dfb1feb7f72af3a5222cc19727df86dd5e46678735404e93a90b1bfa8`；
- 1717 组 ELF symbol 的名称、类型、大小完全一致；1670 组地址不变，配置路径字符串之后的
  51 组地址统一后移 4 B；
- `bk7258_reset_entry=0x0201019c`、`__start=0x02010320`、
  `bk7258_ap_control_initialize=0x02032708`、`_eheap=0x2804fffc` 均不变；
- `.data=0x4b8`、`.bss=0x1efc` 不变，只有含配置路径元数据的 `.text` 从
  `0x2868c` 增至 `0x28690`；
- dual manifest 除 CP raw/CRC 与由其派生的 factory SHA-256 外，布局、偏移、长度、bootloader
  与 AP hash 全部一致。

结论：配置目录归一化没有改变 CP/AP 执行逻辑；`ap_up` 可继续作为 N7 板端回归基线，后续应从
它复制 `ap_smp`，而不是直接覆盖 UP 配置。

## 20. Phase 1-4 目录结构重构与 AP-SMP 配置种子

日期：2026-07-28

本轮完成 `chip/` 按角色拆分、配置目录归一化、生成物清理，并从已验证的 AP-UP 配置创建
`ap_smp` 开发种子。全程未修改 Kconfig role symbol 或执行逻辑，未启用 `CONFIG_SMP`，也未开始
CPU2 bring-up。

### 重构范围

```text
configs/nsh   -> configs/cp_nsh
configs/ap    -> configs/ap_up
chip/         -> chip/common/   (bk7258_allocateheap.c, irq.c, timerisr.c, os_adapt.c,
                                  sdk_stubs.c, lowputc.c, serial.c, clockdiag.h, dvfs.h, sdk_irq.h)
              -> chip/cp/       (vectors.c, start.c, ap_control.c, clock.c/clk_ll.h/clock.h,
                                  dvfs.c, dvfs_procfs.c, flash_mtd.c/.h, wdt.c/.h,
                                  sdk_irq.c, sdk_irq_timer_test.c, gpio_* files)
              -> chip/ap/       (ap_vectors.c, ap_start.c, ap_main.c)
```

Phase 3 已清除团队仓内的 `.bak`、`__pycache__`、`.pyc`、对象/依赖文件和 bootloader
可重建输出；最终生成物扫描为 0。Phase 4 将 `configs/ap_up/` 逐字节复制为
`configs/ap_smp/`，两棵配置树完全一致。当前 `ap_smp` 仍是 `CONFIG_SMP=n` 的 UP 基线副本，
只用于后续独立开发，不能表述为 SMP 已启用或已验证。

### 构建验证

`build_dual_image.sh` exit 0（CP → AP-UP → 恢复 CP）。
构建日志：`/tmp/bk7258-dir-refactor-final-build.log`

SDK IRQ verifier：**RESULT 48 passed, 0 failed**
日志：`/tmp/bk7258-dir-refactor-sdk-irq.log`

### 最终产物哈希

```text
CP app.bin      SHA-256: 18ba5078efc3b8f8a5ed117ec36bf28019ec3162c90ab5dbe56df92f5b411015
CP app_crc.bin  SHA-256: 759a08f634973e2850e05fdca786e7d04f4ebd42933067aa812dd1a0e20ef595
AP app1.bin     SHA-256: 4068a5d5736b6a6ebaf212746bae0f51d72ded0e55a97e38b2671dd20472524e
AP app1_crc.bin SHA-256: 270335090d1efee89de9f556dfb8f50bd574da9b1388055b7df51d1e9bbe7f51
```

### ELF section sizes

```text
CP  .text=166636  .data=5924  .bss=10300
AP  .text=40868   .data=680   .bss=6616
```

### 关键符号地址

```text
__start   CP=0x02010320  AP=0x022002b0
_eheap    CP=0x2804fffc  AP=0x2809effc
CP boot magic @ 0x02010100 = BK7236
```

### 等价性验证

完整等价日志：`/tmp/bk7258-dir-refactor-equivalence.log`

```text
总体：13/13 PASS
CP defined symbols: 1745 entries — exact match
AP defined symbols: 510 entries — exact match
AP raw+CRC: exact byte-for-byte match
CP raw/CRC lengths: exact; 5 raw-byte differences, all inside timestamp-bearing g_version
.vectors (CP+AP): both byte-identical to pre-chip-move baseline (320 B each)
N7 dirty-source transformed-baseline preservation: 33/33 exact
```

配置哈希（未变化）：

```text
cp_nsh/defconfig  7cb6ebf5511c421904116d0c54b98203064a2d3fe03eb31472015da0647d7e75
ap_up/defconfig   8978f9261cdd485deb43368e86eed9388db7278a829b37999103ae70392d3529
ap_smp/defconfig  8978f9261cdd485deb43368e86eed9388db7278a829b37999103ae70392d3529
```

### 重要说明

CP 产物包含编译时间戳（`g_version`）等变化字节，不同构建不会产生完全相同的 raw 哈希。
上述 5 处 raw-byte 差异均位于 `g_version` 区间，不涉及执行逻辑。
**结构不变量**（`.vectors` 逐字节一致、symbol 集精确匹配、长度一致、关键地址不变）
是跨构建等价性的权威判据。

状态：**build/static verified only**；未进行 flash 或板端测试。

## 21. CPU0 间歇性 task-exit HardFault 最终闭环

日期：2026-07-29

状态：`board-verified`（清理无关实验后的四文件最小 team-overlay 修复）

§18 中首次观察到的 CPU0 task-exit fault 后续变为可重复的低概率故障。最终证据链为：

```text
arm_doirq() no-switch 路径返回 NULL
  -> exception_common 从地址 0 恢复寄存器
  -> PC/LR = 0xaaaaaaaa
  -> IACCVIOL / FORCED HardFault
```

保存 NULL fallback 后，又通过 `CFSR.INVPC`、`EXC_RETURN=0xfffffff1` 和 UART1/SysTick
优先级确认当前非 HIPRI dispatcher 被嵌套调用。最终修复在 team overlay 中包装
`arm_doirq()` / `nxsched_resume_scheduler()`，保存清空前的最终 TCB context、对 NULL 返回
fail closed，并通过 BASEPRI 与统一 SDK IRQ 优先级禁止不受支持的普通 IRQ 嵌套。

Flash 时钟、Cache、FPU、SecureFault、RAM vector、4 KiB/顶部 interrupt stack、PRIMASK adapter
以及官方 `arm_schedulesigaction.c` 修改均被板端证据否定并已撤销。最终只保留：

```text
chip/Make.defs
chip/common/bk7258_sdk_irq.h
chip/cp/bk7258_sdk_irq.c
chip/cp/bk7258_vectors.c
```

用户重新编译、下载后确认多次提示符和 `apctl status` 正常，AP 继续保持 READY 且 heartbeat
增长。完整的初学者向解释、UART/J-Link 证据、错误假设时间线和最终代码说明见：

- [`n7-bug-cpu0-task-exit-hardfault.md`](n7-bug-cpu0-task-exit-hardfault.md)
