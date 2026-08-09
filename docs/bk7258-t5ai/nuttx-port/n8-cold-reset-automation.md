# BK7258 N8 cold-reset 自动下载与硬件复位调试

日期：2026-07-30

状态：`implemented / COM7-COM11-verified / manual-reset-cold-verified / jlink-reset-not-equivalent-yet`

## 1. 能力边界

当前 WSL2 环境可以直接调用 Windows 工具，形成以下闭环：

```text
WSL2 build
  -> Windows bk_loader.exe / COM7 下载
  -> Windows COM11 460800 raw capture
  -> marker 自动判读
  -> J-Link RESET pin 自动 physical reset
  -> 再次 raw capture / marker 判读
```

可以自动化：

- CP/AP 双核构建；
- factory 下载和 loader reboot warm path；
- COM11 原始串口采集；
- `BClk/S0/U0/G1/U1/U2/C0..C8/NSH` 判读；
- J-Link RESET pin 拉低/释放产生 physical reset。

不能仅靠现有设备自动化：

- 真正断电重上电；
- 需要人工插拔电源的 power-cycle matrix。

若要自动 power cycle，需要 USB 继电器、可编程电源或可控 USB hub。

## 2. Windows 设备映射

当前 PnP 实测：

```text
COM7  USB-Enhanced-SERIAL-A CH342  -> bk_loader 下载口
COM11 USB-Enhanced-SERIAL-B CH342  -> firmware console，460800 8N1
COM12 JLink CDC UART Port
COM9  USB-SERIAL CH340
```

因此下载必须继续使用：

```text
bk_loader.exe ... -p 7 ...
```

COM11 只用于日志采集，不能把 loader 的 `-p 7` 改成 `-p 11`。

## 3. 脚本

```text
board/bk7258/scripts/bk7258_auto_debug.sh
board/bk7258/scripts/capture_windows_serial.ps1
```

### 3.1 Build + factory 下载 + warm capture

```bash
cd /home/lijian/project/open-vela

./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --build --flash \
  --cp-config cp_nsh \
  --ap-config ap_smp_bidir
```

factory 会把 LittleFS 物理区填充为 `0xff`。交互模式必须输入 `FLASH`；非交互模式必须显式增加 `--yes`。

### 3.2 已构建镜像下载 + warm capture

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --flash
```

### 3.3 J-Link RESET pin 自动 physical reset

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --jlink-reset --capture-seconds 30
```

执行顺序：

```text
打开 COM11
创建 ready marker
J-Link 连接 STAR target
RSetType 2       # RESETPIN
Reset
Go
继续采集至超时
自动判读路标
```

### 3.4 人工 reset 兼容模式

```bash
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_auto_debug.sh \
  --cold-capture --capture-seconds 30
```

脚本提示后人工按 RESET。

## 4. 日志产物

每次运行写入：

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/<timestamp>/
```

主要文件：

```text
serial.raw                 COM11 原始字节
serial.txt                 容错解码文本
serial-capture.stdout.log  PowerShell capture 状态
summary.txt                自动路标判读
download.log               bk_loader 输出
jlink-reset.log            J-Link RESET pin 输出
artifacts.sha256            烧录产物时间/大小/哈希
```

判读示例：

```text
PASS_NSH
STOP_BETWEEN_U1_U2
STOP_AFTER_C8_BEFORE_NSH
STOP_AFTER_<checkpoint>
NO_CHECKPOINT
```

## 5. J-Link 实测

安装：

```text
C:\Program Files\SEGGER\JLink\JLink.exe
J-Link Commander V9.54
probe hardware V9.70
S/N 20790067
VTref 3.295 V
```

SWD 1000 kHz 连接成功：

```text
SW-DP ID: 0x1BE12AEB
CPUID:    0x631F1320
core:     STAR r1p0
```

明确硬件 RESET 使用 Commander 命令：

```text
RSetType 2
Reset
Go
```

J-Link 自身列出的 type 2 是 `RESETPIN`，会使用 RESET pin 复位 core + peripherals。直接 `ClrRESET/SetRESET` 首轮诊断报告 RESET pin 未被拉低，因此改用目标连接后的 reset strategy 2。

注意：

- 以 `-device CORTEX-M33` 连接时，J-Link 会提示实际核心是 STAR；
- 当前 J-Link 探针固件较旧，J-Link 对开启 I/D-cache 的单步/内存调试给出兼容性警告；
- 本自动化当前只使用 RESET pin，不依赖 cache-sensitive 单步能力。

## 6. 当前实测状态

初次 3 秒自检曾因 MobaXterm 占用 COM11 返回 `Access denied`；关闭对应串口 tab 后，COM11 采集已成功验证。

已完成：

```text
COM11 3 秒独占打开自检
COM7 factory 自动下载
warm path raw capture
NSH apctl status 命令发送/回收
手动 physical RESET cold trace
```

下载器打印 `Writing Flash OK / All Finished Successfully`，但进程码可能为 1；主自动化脚本已按完整成功标志归一化。

当前限制：

- J-Link 首次直接拉 RESET pin 未形成 `BClk`，不计为 cold reset；
- `RSetType 2` 保留为实验路径，手动 RESET 仍是 authoritative；
- power cycle 仍需要人工或外部电源控制设备。


## 7. 手动 RESET 与 J-Link 判定标准

2026-07-30 手动 RESET 已取得：

```text
BClk -> JMP -> S0 -> U0 -> G1 -> U1 -> U2..U5
```

这是真实 cold reset，并证明 UART restore 修复生效。

首次 J-Link `ClrRESET/SetRESET` 尝试报告 RESET pin 未拉低，且没有 `BClk/S0`，所以不等价。后续即使使用 `RSetType 2`，也必须以串口是否重新出现 `BClk/JMP/S0` 作为最终判据，不能只看 J-Link 命令返回成功。
