# BK7258 双核自动编译、下载与板端调试 SOP

日期：2026-08-14

状态：`current T5-Board COM3 + P0/P1 SWD/RTT / MCUboot trust preflight verified / older COM7-COM11 evidence retained`

适用项目：

```text
/home/lijian/project/open-vela
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
```

> 当前 T5-Board 硬件覆盖规则：下载只用 COM3；P0/P1 归 SWD/RTT；UART1
> 的 COM4 拨码已关闭且与该调试路由冲突，任何自动流程都不得打开 COM4。
> 文中 COM7/COM11 是旧 T5AI-Core 阶段证据，不得套用到当前板。

问题复盘：[n8-cold-reset-resolution-report.md](n8-cold-reset-resolution-report.md)

## 1. SOP 目标

本 SOP 用于在 WSL2 中完成：

```text
构建 CP/AP 双镜像
  -> 调用 Windows bk_loader 下载
  -> 提前打开 Windows console 采集
  -> 自动保存日志和产物哈希
  -> 手动/J-Link RESET
  -> 自动判断最后启动路标
  -> 必要时发送 NSH 命令
```

默认推荐方式：

- 下载和 warm capture：自动；
- physical cold reset：手动按键；
- J-Link reset：仅实验使用，以是否出现 `BClk` 为判据。

### 1.1 通用性原则

本 SOP 不绑定某一个 AP 固件。双镜像由两个配置名共同决定：

```text
CP_CONFIG_NAME=<CP 配置>
AP_CONFIG_NAME=<AP 配置>
```

历史 cold-reset 修复曾使用阶段快照配置。当前构建入口已经收敛为带物理板名和用途的稳定 profile，完整清单见
[`board/bk7258/configs/README.md`](../../../board/bk7258/configs/README.md)。

通用命令必须显式写出配置：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --build --flash \
  --cp-config <cp_config> \
  --ap-config <ap_config>
```

如果省略：

```text
CP 默认：t5ai_core_cp_base
AP 默认：t5ai_core_ap_base
```

默认值与 `build_dual_image.sh` 一致，但正式验证建议始终显式指定，避免误刷上一次 Stage 的 AP image。

### 1.2 边调试边维护 SOP

调试工具和 SOP 不等到 Stage 结束后一次性补写，按以下两层维护：

- `实验步骤`：正在定位的问题、只读寄存器/共享内存取证、尚未通过重复性验证的判据；
- `正式流程`：已在目标板上重复通过、可作为后续回归门禁的命令和判据。

每次实验必须保留原始日志路径、镜像 profile 和失败现象。单次成功不能直接提升为正式流程；至少完成对应验收矩阵并在清理诊断代码后复测一次。永久保留无串口副作用的共享状态字段是允许的，临时 UART checkpoint 则按第 16 节清理。

### 1.3 通用工具与 BK7258 板级流程的边界

可独立复用的 UART 原始采集、DTR/RTS 同步和 J-Link 保守诊断已经抽到：

```text
../../../tools/windows-hardware-debug/
```

入口：[Windows/WSL2 通用串口与 J-Link 调试 SOP](../../../tools/windows-hardware-debug/README.md)。该目录不包含 BK7258 地址、COM 号、复位极性、loader 或烧录动作，并提供 Claude/Codex 可读取的 `SKILL.md`。

本文件和 `bk7258_auto_debug.sh` 仍是 BK7258 的板级事实来源，负责 CP/AP profile、镜像打包、BK loader、当前 COM3/P0-P1 路由和 BK marker 判据。不要在未完成 BK 板端回归前用通用脚本替换现有已验证流程；新调试动作可先在通用层沉淀，再由本 SOP 固定板级参数和验收证据。

## 2. 固定环境

### 2.1 端口

当前 T5-Board：

```text
COM3  = bk_loader 下载口
P0/P1 = SWD/RTT
COM4  = 禁止打开（UART1 拨码关闭且与 P0/P1 调试路由冲突）
```

下面的 COM7/COM11/COM12 仅保留为旧 T5AI-Core 验证记录：

```text
COM7  = CH342-A 下载口
COM11 = CH342-B firmware console
COM12 = J-Link CDC UART
COM9  = 其他 CH340
```

下载参数：

```text
port:  COM7 / bk_loader -p 7
baud:  6000000
uart:  OTHER
reboot: 1
fast-link: 1
```

Console 参数：

```text
port: COM11
baud: 460800
bits: 8
parity: none
stop: 1
flow control: none
DTR/RTS: false
```

### 2.2 Windows 工具

```text
C:\Users\lijian\Downloads\BEKEN_BKFIL_V2.1.11.15_20241114\BEKEN_BKFIL_V2.1.11.15_20241114\bk_loader.exe
C:\Program Files\SEGGER\JLink\JLink.exe
```

WSL 路径：

```text
/mnt/c/Users/lijian/Downloads/BEKEN_BKFIL_V2.1.11.15_20241114/BEKEN_BKFIL_V2.1.11.15_20241114/bk_loader.exe
/mnt/c/Program Files/SEGGER/JLink/JLink.exe
```

### 2.3 自动化脚本

```text
contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh
contest2026_135_yongwangzhiqian/board/bk7258/scripts/capture_windows_serial.ps1
```

## 3. 每轮开始前的检查

### 3.1 进入工作区

```bash
cd /home/lijian/project/open-vela
```

### 3.2 检查 Windows interop

```bash
command -v powershell.exe
```

必须能找到 `powershell.exe`。

### 3.3 检查串口枚举

```bash
powershell.exe -NoProfile -Command \
  '[System.IO.Ports.SerialPort]::GetPortNames()'
```

预期至少包含：

```text
COM7
COM11
```

### 3.4 检查 COM11 是否被占用

快速只读自检：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --cold-capture --capture-seconds 3
```

成功打开但没有数据时，预期：

```text
serial_bytes=0
verdict=NO_CHECKPOINT
```

这不代表故障，只代表 3 秒内板卡没有输出。

若出现：

```text
SerialPort.Open(COM11): Access denied
```

处理：

1. 关闭 MobaXterm、PuTTY、串口助手中连接 COM11 的 tab；
2. 不需要关闭整个终端程序；
3. 重新执行 3 秒自检；
4. 不要一边让 MobaXterm 占用 COM11，一边运行自动采集。

## 4. 标准构建流程

### 4.1 通用构建模板

```bash
cd /home/lijian/project/open-vela

CP_CONFIG_NAME=<cp_config> \
AP_CONFIG_NAME=<ap_config> \
  ./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

T5AI-Core 基础实例：

```bash
CP_CONFIG_NAME=t5ai_core_cp_base \
AP_CONFIG_NAME=t5ai_core_ap_base \
  ./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

Builder 默认值：

```text
CP_CONFIG_NAME=t5ai_core_cp_base
AP_CONFIG_NAME=t5ai_core_ap_base
```

成功门禁：

```text
build_dual_image: artifacts: .../nuttx/bk7258-dual
build_dual_image: root CP artifacts match the manifest CP image
```

### 4.2 可用配置选择表

| 物理板/用途 | CP | AP |
|---|---|---|
| T5AI-Core 基础 | `t5ai_core_cp_base` | `t5ai_core_ap_base` |
| T5AI-Core MCUboot | `t5ai_core_cp_mcuboot` | `t5ai_core_ap_mcuboot` |
| T5AI-Core PSRAM/SMP/BLE 验证 | `t5ai_core_cp_psram_validation` | `t5ai_core_ap_psram_validation` |
| T5AI-Core Wi-Fi | `t5ai_core_cp_wifi` | `t5ai_core_ap_wifi` |
| T5-Board 应用 | `t5_board_cp_app_mcuboot` | `t5_board_ap_app_mcuboot` |
| T5-Board camera/H.264 | `t5_board_cp_app_mcuboot` | `t5_board_ap_camera_h264_mcuboot` |
| T5-Board Wi-Fi | `t5_board_cp_wifi_mcuboot` | `t5_board_ap_wifi_mcuboot` |

T5-Board 还保留 camera smoke、PWM 等有界 validation profile，以及不可烧板的 drivercheck CI pair；详见配置目录 README。阶段性的 SMP affinity、semaphore、migration 等旧 profile 已完成使命，其能力并入当前验证档案，不再作为独立 defconfig 维护。

配置必须来自：

```text
contest2026_135_yongwangzhiqian/board/bk7258/configs/
```

Builder 按每个目录的 `profile.conf` 检查物理板、CP/AP role、boot 格式和兼容组，不再维护容易漂移的文件名白名单。

### 4.3 构建产物

```text
nuttx/bk7258-dual/bl_crc.bin
nuttx/bk7258-dual/app.bin
nuttx/bk7258-dual/app_crc.bin
nuttx/bk7258-dual/app1.bin
nuttx/bk7258-dual/app1_crc.bin
nuttx/bk7258-dual/nuttx-cp.elf
nuttx/bk7258-dual/nuttx-ap.elf
nuttx/bk7258-dual/bk7258-dual-image.json
nuttx/bk7258-dual/bk7258-trust-chain.json   # MCUboot profile only
nuttx/bk7258-dual/build-profile.txt
nuttx/bk7258-dual/all-app-factory.bin
nuttx/bk7258-dual/littlefs_factory_clear.bin
nuttx/bk7258-dual/bk7258-ab-layout.json
nuttx/bk7258-dual/bk7258-factory-layout.json
```

### 4.4 产物代际检查

```bash
stat -c '%y %s %n' \
  contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_serial.c \
  nuttx/bk7258-dual/nuttx-cp.elf \
  nuttx/bk7258-dual/app_crc.bin \
  nuttx/bk7258-dual/all-app-factory.bin

sha256sum \
  nuttx/bk7258-dual/app.bin \
  nuttx/bk7258-dual/app_crc.bin \
  nuttx/bk7258-dual/app1.bin \
  nuttx/bk7258-dual/app1_crc.bin \
  nuttx/bk7258-dual/all-app-factory.bin \
  nuttx/bk7258-dual/littlefs_factory_clear.bin
```

同时检查 profile：

```bash
cat nuttx/bk7258-dual/build-profile.txt
```

预期明确记录：

```text
CP_CONFIG_NAME=...
AP_CONFIG_NAME=...
```

规则：

- 不同 AP Stage 必须选择对应 `AP_CONFIG_NAME`，不能复用 SOP 示例名；
- `build-profile.txt` 必须与本轮计划验证的固件组合一致；
- factory 时间必须晚于本轮源码修改；
- 每次实质性重编后记录新哈希；
- 不允许拿旧哈希代替新产物验证；
- 烧录日志中的路径必须与刚检查的 factory 路径一致。

### 4.5 历史 pre-N15 cold-reset factory

```text
size:   2298910 bytes
sha256: d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405
```

该哈希只属于历史源码快照，不得用于当前ADR-004布局。后续重编必须重新计算。

## 5. 自动 Build + sparse 下载 + warm capture

当前 T5-Board 日常应用更新推荐只写 CP/AP：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --build \
  --flash \
  --sparse-flash \
  --apps-only \
  --no-console \
  --cp-config t5_board_cp_app_mcuboot \
  --ap-config t5_board_ap_app_mcuboot
```

验证其他用途时，必须替换成同一物理板、同一 boot 格式和同一兼容组的 CP/AP profile，其他下载和采集流程不变。

流程：

1. 按显式 `--cp-config/--ap-config` 构建 CP/AP；
2. 校验 package manifest、ELF/raw 绑定和公开信任契约；
3. 通过 P0/P1 对三个身份分别读取固定地址和现板兼容地址，共六段非停核
   J-Link 读取；每个身份只需一个允许地址精确匹配；
4. J-Link 失败，或任一身份在全部允许地址上都不匹配时，在调用 loader 前失败；
5. 记录本轮 CP/AP sparse artifacts、契约和预检结果的 SHA-256；
6. 仅通过 COM3 调用 Windows loader 写 CP/AP；
7. 下载后 `--reboot 1`；
8. 通过 RTT/后续功能检查取得启动和运行证据，不打开 COM4。

N15-M之后，普通开发/回归必须使用`--sparse-flash --apps-only`，它只更新CP/AP并保留
BL1、BL2、Manifest、B、LittleFS、`usr_config`、reserved和official tail。普通
`--sparse-flash`仍包含boot-chain段，只能在对应范围获得明确授权时使用。省略`--sparse-flash`会进入destructive factory
rewrite并清空LittleFS，只能在owner重新明确授权后使用。该路径交互执行时必须输入：

```text
FLASH
```

无人值守时必须显式增加：

```text
--yes
```

不要把`--yes`用于普通开发，也不要在没有当前破坏性授权时使用。

## 6. 只下载已构建镜像

flash-only 模式不会重新决定 AP 固件，而是读取：

```text
nuttx/bk7258-dual/build-profile.txt
```

并在启动时打印实际 packaged profile。可以传入 `--cp-config/--ap-config` 作为期望值门禁；若与已打包 profile 不同，脚本会拒绝下载。

推荐：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --flash \
  --sparse-flash \
  --apps-only \
  --no-console \
  --cp-config t5_board_cp_app_mcuboot \
  --ap-config t5_board_ap_app_mcuboot
```

不指定期望 profile 时（仍使用 package 中记录的配对）：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --flash \
  --sparse-flash \
  --apps-only \
  --no-console
```

无人值守的CP/AP-only sparse更新不需要`--yes`，但信任预检不可跳过：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --flash --sparse-flash --apps-only --no-console
```

新构建的固定身份块位于 BL1 `0x0200fd40/0x0200fd60` 和 BL2
`0x024d2f00`；当前板尚未重写 boot chain，因此固定块为擦除态，继续在已审查的
兼容地址 `0x02002774/0x02002754/0x024d27ec` 匹配。脚本会同时只读探测两组，
不会因为兼容匹配而写 BL1/BL2，也没有自动轮换根密钥的路径。

## 7. Windows CMD 手动 factory 下载备用命令

下列命令是N15-M已经执行过的一次性破坏性迁移/后续factory rewrite模板，不是日常更新命令。
再次执行前必须取得新的owner授权；普通固件更新用第5/6节的`sparse-flash`。

在 Windows CMD 中：

```bat
cd /d C:\Users\lijian\Downloads\BEKEN_BKFIL_V2.1.11.15_20241114\BEKEN_BKFIL_V2.1.11.15_20241114

bk_loader.exe download -p 7 -b 6000000 --uart-type OTHER ^
  --mainBin-multi //wsl.localhost/Ubuntu-22.04/home/lijian/project/open-vela/nuttx/bk7258-dual/all-app-factory.bin@0x0-0x4fc000,//wsl.localhost/Ubuntu-22.04/home/lijian/project/open-vela/nuttx/bk7258-dual/littlefs_factory_clear.bin@0x600000-0x100000 ^
  --reboot 1 --fast-link 1
```

factory命令只覆盖上述两段。`usr_config` `0x4fc000..0x50a000`、两段之间的
reserved区域以及`0x7fa000..0x800000`校准尾区都不能出现在loader参数中；禁止使用chip erase。

### 7.1 BKFIL read-back验收规则

不要使用6 Mbps Flash read作为位精确备份。N15-M取证证明该模式可能偶发插入128-byte全零块。
需要验证`usr_config`、calibration tail或其他关键区时：

1. 将BKFIL read baud固定为115200；
2. 对完全相同的offset/length连续读取两次；
3. 用`cmp`和SHA-256确认两份byte-identical；
4. 只有满足重复一致的文件才可作为验收证据；恢复镜像还需单独验证布局和完整性。

迁移前的6 Mbps 8 MiB dump只作forensic reference，禁止直接回刷。

成功标志：

```text
Writing Flash OK
{All Finished Successfully}
```

注意：当前版本 loader 可能在打印成功后仍返回进程码 1。自动脚本已兼容；手工判断时应同时检查完整成功标志和 flash verify，而不是只看 `%ERRORLEVEL%`。

## 8. Warm path 验收

下载后预期：

```text
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
S0 U0 G1 U1
GPIO device 0x21
U2 U3 U4 U5
C0 C1 C2 C3
A0..A6
W0 W1
A7 F1 F2
C4 C5 C6 C7 C8
NuttShell (NSH)
nsh>
```

历史 BP2P 镜像可能等待数秒后走 `F1/F2` timeout cleanup；该说明仅用于解释旧日志，不是当前 profile 的通过条件。

## 9. Physical cold-reset 标准流程

### 9.1 推荐：手动按键 RESET

先运行采集：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --cold-capture \
  --capture-seconds 30
```

看到提示：

```text
Serial capture is ready. Press the board physical RESET now.
```

立即按下并释放板卡 RESET。

有效 cold trace 必须包含：

```text
u_bootloader enter
BClk A5=... A9=...
partition app @ 0x02010000
jump to:0x02010000
JMP
S0
```

没有 `BClk`，不得把该次运行记为 physical cold reset。

### 9.2 第一级：CP/NSH cold-reset 门禁

```text
BClk
S0 U0 G1 U1
U2 U3 U4 U5
C0 C1 C2 C3
A0..A6
W0 W1
A7 F1 F2
C4 C5 C6 C7 C8
NuttShell (NSH)
```

这一级只证明 bootloader、CP NuttX 和 NSH 可用。`PASS_NSH` **不证明** AP 已 READY，也不证明 RPTUN/RPMsg 已连接。

### 9.3 第二级：当前 profile 的功能门禁

使用 `t5ai_core_cp_psram_validation + t5ai_core_ap_psram_validation` 时，物理复位后还必须在 NSH 执行：

```text
apctl status
bkrpmsgtest run 100 64 idle 10000
```

RPTUN profile 的成功条件是：

```text
AP state=READY
RPTUN state=CONNECTED
BRPT ... CPU0 sent=100 received=100 errors=0
BRPT ... CPU1 sent=100 received=100 errors=0
BRPT PASS
```

若得到 `transport=-107`（`ENOTCONN`），即使自动脚本已经输出 `PASS_NSH`，本次 N9 cold-reset 仍判定失败。

## 10. J-Link reset：实验流程

J-Link 已成功连接目标：

```text
probe: J-Link V9 / S/N 20790067
VTref: ~3.29 V
SWD:   1000 kHz
CPUID: 0x631F1320
core:  STAR r1p0
```

脚本命令：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --jlink-reset --capture-seconds 30
```

内部策略：

```text
RSetType 2   # RESETPIN
Reset
Go
```

限制：

- 首次直接 `ClrRESET/SetRESET` 实测未能拉低 RESET pin；
- 即使 J-Link 命令没有报错，也必须以串口出现 `BClk/JMP/S0` 为最终判据；
- 未出现 `BClk` 时，本次 J-Link 运行不计入 cold-reset 矩阵；
- 当前探针固件对带 I/D-cache 的单步调试有兼容性警告，但 RESET pin 操作与此分开。

### 10.1 N9 RPTUN 共享控制块只读取证（实验步骤）

当 NSH 可用但 RPMsg 未连接时，先保留失败现场，再读取 `0x28097000` 的 16 个 word：

```bash
/bin/bash -lc "printf 'halt\nmem32 0x28097000,16\ngo\nexit\n' | \
  '/mnt/c/Program Files/SEGGER/JLink/JLink.exe' \
  -device CORTEX-M33 -if SWD -speed 1000 -autoconnect 1"
```

控制块按 word 解释：

| 偏移 | 字段 | 说明 |
|---:|---|---|
| `0x00` | magic | 期望 `0x54505242` |
| `0x0c` | generation | CP/AP 生命周期代际 |
| `0x10` | state | `0..6` = OFFLINE/PREPARING/TABLE_READY/CONNECTING/CONNECTED/QUIESCING/FAULTED |
| `0x14` | flags | AP 初始化进度位，见下表 |
| `0x18` | error | RPTUN wrapper 错误 |
| `0x20/0x24` | pending | CP→AP / AP→CP 待处理通知位图 |
| `0x30/0x34` | epoch | CP / AP 已接受的 generation |

`flags` 当前定义：

| 位 | 名称 | 已完成阶段 |
|---:|---|---|
| `0x01` | AP_MBOX_ENTER | 进入 SDK mailbox wrapper |
| `0x02` | AP_MBOX_READY | SDK mailbox wrapper 已返回 |
| `0x04` | AP_RPTUN_ENTER | 进入 board RPTUN wrapper |
| `0x20` | AP_CORE_READY | NuttX `rptun_initialize()` 已返回 |
| `0x40` | AP_TEST_ENTER | 进入 RPMsg 测试服务初始化 |
| `0x80` | AP_TEST_READY | RPMsg 测试服务初始化已返回 |
| `0x08` | AP_RPTUN_READY | board RPTUN wrapper 已返回 |
| `0x10` | AP_READY | AP READY 已发布 |

mailbox wrapper 内部细分位为：`0x100` semaphore ready、`0x200` worker created、`0x400` worker pinned、`0x800` SDK logical channel initialized、`0x1000` channel opened、`0x2000` callbacks installed。正常完成后的 flags 为 `0x00003fff`。

例如，物理复位失败现场的 `state=3, flags=0x7, cp_to_ap_pending=3, ap_epoch=0` 表示：mailbox wrapper 已完成，AP 已进入 RPTUN wrapper，但尚未完成其内部初始化；不能再归因于 mailbox 初始化或单纯增加 AP 启动超时。

## 11. 在 NSH 下采集状态

COM11 空闲且 NSH 已出现时，可以使用 PowerShell capture 脚本发送命令：

```bash
cd /home/lijian/project/open-vela

PS1=$(wslpath -w \
  contest2026_135_yongwangzhiqian/board/bk7258/scripts/capture_windows_serial.ps1)
OUT=$(wslpath -w /home/lijian/project/open-vela/logs/apctl-status.raw)

powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$PS1" \
  -Port COM11 \
  -Baud 460800 \
  -DurationSec 6 \
  -OutputFile "$OUT" \
  -Command 'apctl status'
```

常用命令：

```text
apctl status
uname -a
ps
ls /dev
cat /data/probe.txt
```

N9 配置下，`apctl status` 会直接输出：

```text
RPTUN state=CONNECTED(4) error=0 generation=...
RPTUN pending cp/ap=... heartbeat cp/ap=... epoch cp/ap=...
```

`apctl restart` 命令会在 AP READY 后立即打印一次状态；Name Service callback 尚未调度时，
这一次允许短暂看到 `CONNECTING(3)`。随后单独执行 `apctl status` 必须转为
`CONNECTED(4)`。迁移点是 CP 成功绑定 AP endpoint，不是单纯 lower-half 初始化完成；
若持续停在 CONNECTING，再执行 `bkrpmsgtest` 和第 10.1 节控制块读取。

只有 CP 无法继续执行 NSH 命令时，才需要退回第 10.1 节的 J-Link 共享内存读取。

## 12. 自动日志目录

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/<timestamp>/
```

文件：

```text
artifacts.sha256            requested/packaged CP/AP profile、烧录产物时间/大小/哈希
download.log                bk_loader 完整输出
serial.ready                COM11 已打开标志
serial.raw                  原始 UART 字节
serial.txt                  容错文本解码
serial-capture.stdout.log   Windows capture 状态
summary.txt                 自动判读
jlink-reset.log             J-Link 输出，仅 jlink-reset 模式
```

## 13. Marker 判读表

| 最后结果 | 含义 | 下一步 |
|---|---|---|
| 无串口字节 | 未复位、端口/接线错误或 capture 太短 | 检查 COM11、波特率、按键时机 |
| `BClk` 后无 `JMP` | bootloader cold clock/partition/jump | 查 bootloader |
| `U1` 后无 `U2` | `bk_uart_init()` 窗口 | 查 GPIO/UART clock/config |
| 有 `U2..U5` | UART setup 和 console 注册通过 | 不再回退 UART 修复 |
| `C0` 后无 `C1` | WDT init | 查 WDT handoff |
| `W0` 后无 `W1` | 第一次 sleep/SysTick | 查 timer/tick |
| `W1` 后数秒出现 `A7/F1/F2` | AP READY timeout cleanup | 继续看是否 fail-open 到 NSH |
| `F1/F2` 后到 `C8/NSH` | AP timeout，但 CP 启动成功 | AP 作为独立问题 |
| `C8` 后无 NSH | board init 已返回，NSH session/stdio 问题 | 查 NSH console |
| `PASS_NSH` | 本次启动进入 NSH，仅证明 CP shell | 记录 cold/warm 属性，再执行当前 profile 功能门禁 |
| `PASS_NSH` 后 `ENOTCONN` | CP 正常但 RPTUN 未连接 | `apctl status`，再按 10.1 读取控制块 |
| `BRPT PASS` | 当前 RPMsg 功能用例通过 | 记录 count/payload/load/generation |

当前 summary 额外输出：

```text
cold_path=yes/no
uart_init_returned=yes/no
ap_timeout_cleanup=yes/no
nsh=yes/no
```

## 14. 常见故障处理

### 14.1 COM11 access denied

原因：MobaXterm/串口助手占用。

处理：关闭 COM11 tab，再重试。

### 14.2 COM7 loader connect fail

检查：

```text
CH342-A 是否仍为 COM7
板卡供电
下载/BOOT 接线
是否有另一个 BKFIL/bk_loader 正在占用 COM7
```

### 14.3 Loader 打印成功但脚本曾报 exit 1

新版自动脚本已按以下两个标志归一化：

```text
Writing Flash OK
{All Finished Successfully}
```

如果两者都存在，脚本将退出成功并给出 warning，而不是误报下载失败。

### 14.4 J-Link 报 RESET high but should be low

说明 J-Link 未确认 RESET pin 被拉低。

处理：

- 检查 J-Link RESET 与 SoC nRESET 接线；
- 检查电平和共地；
- 使用 `RSetType 2`；
- 仍以 `BClk` 为最终判据；
- 必要时继续使用手动 RESET。

### 14.5 `[ipc_svr] create_socket failed.`

若它出现在 `NuttShell (NSH)` 之后，不是启动 blocker。本 Stage 不处理。

### 14.6 N9 cold reset 后 `ENOTCONN`

已验证的故障签名：

```text
PASS_NSH
bkrpmsgtest ... -> transport=-107
RPTUN state=CONNECTING
flags=0x00000001 或 0x00000007
```

根因不是 SDK/NuttX 源码错误，也不是单纯启动超时：AP init task 优先级 100，在同步 `kthread_create()` 激活优先级 225 的 mailbox RX worker、优先级 224 的 stock RPTUN worker后，冷启动时初始化协调者可能无法可靠返回。warm restart 的成功会掩盖这个问题。

board wrapper 的已验证处理是：

- SDK 物理 MBOX0 仍在 AP SMP bootstrap 早期初始化；
- N8 AP-local SMP gate 先完成，避免 logical RPMsg traffic 干扰零长度 IPI 自测；
- 创建 logical mailbox/RPTUN workers 期间，AP 初始化协调者临时使用优先级 226；
- 发布 AP/RPTUN READY 后恢复原优先级；
- 不修改官方 NuttX，也不修改 SDK 源码。

对应板端证据为 3 次独立 physical RESET 均 `BRPT PASS`。最终正式打包产物又通过一次
physical RESET（`logs/bk7258-auto-debug/20260731-215631/`，`cold_path=yes`），随后
`logs/n9-final-packaged-apctl-status.raw` 得到 generation 1、flags `0x3fff`、
`CONNECTED(4)`；`logs/n9-final-packaged-rpmsg-run100.raw` 中双 AP CPU 各 100/100。
warm restart 后 generation 2 再次 CONNECTED、各 100/100，并由
`logs/n9-final-connected-warm-syslog.raw` 证明 `syslog_rpmsg` 继续通过。

## 15. 重复性验收矩阵

| 启动方式 | 次数 | 必须检查 |
|---|---:|---|
| factory 下载后 warm 启动 | 1 | `U2/U5/C8/NSH` |
| loader reboot | 3 | NSH 可重复出现 |
| 手动 physical RESET | 3 | 每次有 `BClk/U2/C8/NSH` |
| 断电重上电 | 3 | 与 physical RESET 相同 |
| NSH `apctl status` | 每类至少 1 | 区分 CP 成功与 AP timeout |
| N9 physical RESET 后 RPMsg | 3 | `AP READY`、`RPTUN CONNECTED`、双 AP CPU 各 100/100、`BRPT PASS` |
| N9 AP warm restart | 3+ | generation 递增、旧事件不误判、重连后 `BRPT PASS` |
| LittleFS | 1+ | factory 擦除语义或 split update 保持语义符合预期 |

## 16. 调试结束后的清理

重复性矩阵通过后，删除临时诊断代码：

```text
cold_ckpt() helper
S/U/G/E/C/A/W/F checkpoint
bk7258_ap_wait() 的 first_iter 诊断逻辑
```

必须保留真实功能代码：

```text
bk_gpio_driver_init() 先于 bk_uart_init()
bk7258_uart_restore_console()，若最终产品回归仍证明必要
WDT-before-AP
AP deterministic failure cleanup
mailbox/SMP ABI
flash/LittleFS 已验证语义
```

清理后重新构建，并至少再做一次手动 physical RESET：

```text
BClk -> U2 等价功能 -> C8 -> NSH
```

产品版不会再打印 U/C/A 路标，因此应以 bootloader、NSH 和功能命令作为最终门禁。

## 17. 一页式执行清单

```text
[ ] cd /home/lijian/project/open-vela
[ ] 确认当前板只用 COM3 下载，COM4 未打开
[ ] 确认 P0/P1 SWD/RTT 与 J-Link 可读
[ ] 明确选择 CP_CONFIG_NAME 和 AP_CONFIG_NAME
[ ] 使用显式 profile 运行 build_dual_image.sh
[ ] 检查 bk7258-dual/build-profile.txt
[ ] stat + sha256sum 新产物
[ ] 日常更新：auto_debug.sh --flash --sparse-flash --apps-only --no-console
[ ] 检查 trust-preflight.json 为 pass，随后核对 RTT/NSH/功能状态
[ ] auto_debug.sh --cold-capture --capture-seconds 30
[ ] 手动按 RESET
[ ] 检查 BClk/U2/C8/NSH
[ ] NSH 下执行 apctl status
[ ] RPTUN profile 下执行 bkrpmsgtest，不能用 PASS_NSH 代替 BRPT PASS
[ ] 失败现场先归档，再只读采集 0x28097000 控制块
[ ] 归档 logs/bk7258-auto-debug/<timestamp>
[ ] 更新当前 Stage worklog
[ ] 重复性矩阵通过后再清理 checkpoint
```
