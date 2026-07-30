# BK7258 双核自动编译、下载与板端调试 SOP

日期：2026-07-30

状态：`usable / COM7-COM11 verified / manual cold-reset verified / J-Link reset experimental`

适用项目：

```text
/home/lijian/project/open-vela
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
```

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

本次 cold-reset 修复使用 `cp_nsh + ap_smp_bidir`，它只是一个实例，不是全项目固定值。

通用命令必须显式写出配置：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
  --build --flash \
  --cp-config <cp_config> \
  --ap-config <ap_config>
```

如果省略：

```text
CP 默认：cp_nsh
AP 默认：ap_smp
```

默认值与 `build_dual_image.sh` 一致，但正式验证建议始终显式指定，避免误刷上一次 Stage 的 AP image。

## 2. 固定环境

### 2.1 端口

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
contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh
contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/capture_windows_serial.ps1
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
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
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
  ./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

本次 cold-reset 实例：

```bash
CP_CONFIG_NAME=cp_nsh \
AP_CONFIG_NAME=ap_smp_bidir \
  ./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

Builder 默认值：

```text
CP_CONFIG_NAME=cp_nsh
AP_CONFIG_NAME=ap_smp
```

成功门禁：

```text
build_dual_image: artifacts: .../nuttx/bk7258-dual
build_dual_image: root CP artifacts match the manifest CP image
```

### 4.2 可用配置选择表

CP 配置：

| 配置 | 用途 |
|---|---|
| `cp_nsh` | CP NSH + AP control + AP autostart |
| `cp_nsh_manual` | CP NSH + AP control，不自动启动 AP；用于手工 `apctl start`/分层调试 |

AP 配置：

| 配置 | 主要 Stage / 用途 |
|---|---|
| `ap_up` | CPU1 单核 AP NuttX |
| `ap_smp` | AP SMP secondary bootstrap 基线 |
| `ap_smp_online` | CPU2 scheduler-online IDLE |
| `ap_smp_affinity` | CPU1 explicit-affinity task |
| `ap_smp_semwake` | 单次 semaphore remote wake |
| `ap_smp_semwake_loop` | 固定 8 轮 semaphore wake loop |
| `ap_smp_bidir` | 双向 semaphore pingpong / BP2P |
| `ap_smp_dualtask` | 两个 CPU1 task 本地调度 |
| `ap_smp_migration` | controlled migration |
| `ap_smp_timedwait` | CPU1 timed wake |
| `ap_smp_lifecycle` | scheduler quiesce/resume foundation |

常用组合：

```text
自动启动某 AP Stage： cp_nsh        + 对应 ap_* 配置
手工启动/重试 AP：    cp_nsh_manual + 对应 ap_* 配置
```

示例：

```text
基础 SMP bootstrap：      cp_nsh        + ap_smp
本次 cold-reset/BP2P：    cp_nsh        + ap_smp_bidir
手工 retry BP2P：         cp_nsh_manual + ap_smp_bidir
controlled migration：    cp_nsh        + ap_smp_migration
scheduler lifecycle D1：  cp_nsh        + ap_smp_lifecycle
```

配置必须来自：

```text
contest2026_135_yongwangzhiqian/board/bk7258_t5ai/configs/
```

Builder 会拒绝不在白名单中的配置名。

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
nuttx/bk7258-dual/build-profile.txt
nuttx/bk7258-dual/all-app-factory.bin
```

### 4.4 产物代际检查

```bash
stat -c '%y %s %n' \
  contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/bk7258_serial.c \
  nuttx/bk7258-dual/nuttx-cp.elf \
  nuttx/bk7258-dual/app_crc.bin \
  nuttx/bk7258-dual/all-app-factory.bin

sha256sum \
  nuttx/bk7258-dual/app.bin \
  nuttx/bk7258-dual/app_crc.bin \
  nuttx/bk7258-dual/app1.bin \
  nuttx/bk7258-dual/app1_crc.bin \
  nuttx/bk7258-dual/all-app-factory.bin
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

### 4.5 本次 cold-reset 已验证 factory

```text
size:   2298910 bytes
sha256: d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405
```

只对当前源码快照有效。后续重编必须重新计算。

## 5. 自动 Build + 下载 + warm capture

推荐一条命令完成：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
  --build \
  --flash \
  --cp-config cp_nsh \
  --ap-config ap_smp_bidir \
  --capture-seconds 30
```

其中 `cp_nsh/ap_smp_bidir` 仅为本次实例。验证其他 Stage 时，替换两个配置参数，其他下载和采集流程不变。

流程：

1. 按显式 `--cp-config/--ap-config` 构建 CP/AP；
2. 记录 factory SHA-256 和 stat；
3. 打开 COM11；
4. 创建 `serial.ready`；
5. 通过 COM7 调用 Windows loader；
6. 下载后 `--reboot 1`；
7. COM11 继续捕获；
8. 生成 marker summary。

factory 会用 `0xff` 覆盖 LittleFS 区。交互执行时必须输入：

```text
FLASH
```

无人值守时必须显式增加：

```text
--yes
```

不要在不了解 LittleFS 影响时随意使用 `--yes`。

## 6. 只下载已构建镜像

flash-only 模式不会重新决定 AP 固件，而是读取：

```text
nuttx/bk7258-dual/build-profile.txt
```

并在启动时打印实际 packaged profile。可以传入 `--cp-config/--ap-config` 作为期望值门禁；若与已打包 profile 不同，脚本会拒绝下载。

推荐：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
  --flash \
  --cp-config cp_nsh \
  --ap-config ap_smp_bidir \
  --capture-seconds 30
```

不指定期望 profile 时：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
  --flash \
  --capture-seconds 30
```

无人值守：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
  --flash --yes --capture-seconds 30
```

## 7. Windows CMD 手动下载备用命令

在 Windows CMD 中：

```bat
cd /d C:\Users\lijian\Downloads\BEKEN_BKFIL_V2.1.11.15_20241114\BEKEN_BKFIL_V2.1.11.15_20241114

bk_loader.exe download -p 7 -b 6000000 --uart-type OTHER ^
  --mainBin-multi //wsl.localhost/Ubuntu-22.04/home/lijian/project/open-vela/nuttx/bk7258-dual/all-app-factory.bin@0x0 ^
  --reboot 1 --fast-link 1
```

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

当前 `ap_smp_bidir` 可能等待数秒后走 `F1/F2` timeout cleanup；只要继续到 `C8/NSH`，CP fail-open 就是正常的。

## 9. Physical cold-reset 标准流程

### 9.1 推荐：手动按键 RESET

先运行采集：

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
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

### 9.2 当前 cold-reset 成功门禁

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
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/bk7258_auto_debug.sh \
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

## 11. 在 NSH 下采集状态

COM11 空闲且 NSH 已出现时，可以使用 PowerShell capture 脚本发送命令：

```bash
cd /home/lijian/project/open-vela

PS1=$(wslpath -w \
  contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/capture_windows_serial.ps1)
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
| `PASS_NSH` | 本次启动进入 NSH | 记录 cold/warm 属性 |

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

## 15. 重复性验收矩阵

| 启动方式 | 次数 | 必须检查 |
|---|---:|---|
| factory 下载后 warm 启动 | 1 | `U2/U5/C8/NSH` |
| loader reboot | 3 | NSH 可重复出现 |
| 手动 physical RESET | 3 | 每次有 `BClk/U2/C8/NSH` |
| 断电重上电 | 3 | 与 physical RESET 相同 |
| NSH `apctl status` | 每类至少 1 | 区分 CP 成功与 AP timeout |
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
[ ] 关闭占用 COM11 的终端 tab
[ ] COM11 3 秒自检通过
[ ] 明确选择 CP_CONFIG_NAME 和 AP_CONFIG_NAME
[ ] 使用显式 profile 运行 build_dual_image.sh
[ ] 检查 bk7258-dual/build-profile.txt
[ ] stat + sha256sum 新产物
[ ] auto_debug.sh --flash --capture-seconds 30
[ ] 检查 U2/U5/C8/NSH
[ ] auto_debug.sh --cold-capture --capture-seconds 30
[ ] 手动按 RESET
[ ] 检查 BClk/U2/C8/NSH
[ ] NSH 下执行 apctl status
[ ] 归档 logs/bk7258-auto-debug/<timestamp>
[ ] 更新当前 Stage worklog
[ ] 重复性矩阵通过后再清理 checkpoint
```
