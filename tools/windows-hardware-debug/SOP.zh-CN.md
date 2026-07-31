# Windows / WSL2 串口与 J-Link 通用调试 SOP

## 1. 目标与边界

本 SOP 解决三类可重复问题：

1. 完整保留设备上电、复位和运行期的 UART 原始字节；
2. 保证“先打开串口，再触发复位”，避免丢失早期启动日志；
3. 对 J-Link 的寄存器、内存读取和复位操作生成可审计的命令、日志和 JSON 结果。

工具不包含烧录、擦除、内存写入、Option Byte、Fuse 或安全状态修改。
如果确实需要这些操作，应由具体芯片项目另行编写和评审 SOP，不要把它们加入通用诊断流程。

## 2. 环境准备

### 2.1 Windows

- 安装 USB 转串口驱动，确认设备管理器中能看到 COM 口；
- 系统自带 Windows PowerShell 5.1 即可，也支持 PowerShell 7；
- 需要 J-Link 时，从 SEGGER 官方安装 J-Link Software；
- 将本目录完整保留，不要只复制单个脚本。

当前 PowerShell 会话可临时允许本地脚本：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

这不会修改系统级执行策略。

### 2.2 WSL2

COM 口仍由 Windows 打开。不要同时使用 Linux `ttyS*`、Windows 串口工具和本工具争抢同一个端口。

检查互操作：

```bash
powershell.exe -NoProfile -Command '$PSVersionTable.PSVersion'
wslpath -w "$PWD"
```

如果企业策略关闭了 WSL Windows-executable interop，应直接在 Windows PowerShell 中运行脚本。

## 3. 建立端口和目标基线

先列举 Windows 能看到的串口：

```powershell
.\scripts\serial_capture.ps1 -ListPorts
```

WSL2：

```bash
./scripts/debug_session_wsl.sh --list-ports
```

在板级记录中明确以下事实，不要写进通用脚本：

| 项目 | 示例 | 必须如何确认 |
| --- | --- | --- |
| Console port | COM11 | 拔插设备或查看 USB VID/PID |
| Reset/control port | COM7 或同一 COM11 | 查原理图和实际连线 |
| Baud | 115200 | 固件配置或已知正确日志 |
| DTR/RTS 极性 | Asserted | 原理图、示波器或已验证脚本 |
| J-Link device | CORTEX-M33 | 芯片文档或 SEGGER device list |
| 只读地址 | 0x20000000 | 链接脚本、芯片手册或 map 文件 |
| 成功证据 | `boot complete` | 目标端稳定且唯一的输出 |

COM 号、复位极性和地址只要有一个不确定，就先做纯串口采集，不触发目标控制。

## 4. 串口采集

### 4.1 只采集，不控制目标

```powershell
.\scripts\serial_capture.ps1 `
  -Port COM11 -Baud 115200 -DurationSec 20 `
  -OutputFile .\logs\serial.raw `
  -SummaryFile .\logs\serial.json
```

`serial.raw` 是证据原件；不要用终端复制文本替代它。脚本不会自动推断编码。

需要发送无敏感信息的控制台命令时：

```powershell
.\scripts\serial_capture.ps1 `
  -Port COM11 -Baud 115200 -DurationSec 5 `
  -OutputFile .\logs\version.raw `
  -Command 'version' -LineEnding CRLF
```

不要在命令参数中发送密码、密钥或令牌；它们可能进入 shell history 和日志。

### 4.2 人工复位同步采集

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -Baud 115200 -DurationSec 20 `
  -Action ManualReset `
  -ExpectedRegex 'boot complete' `
  -FailRegex 'HardFault|ASSERT'
```

出现 `SERIAL_READY` 和 `MANUAL_ACTION_REQUIRED` 后人工复位。最终只有命中成功表达式且未命中失败表达式，状态才是 `passed`。

## 5. DTR/RTS 复位同步采集

DTR/RTS 会改变目标控制线，可能导致复位或进入 Bootloader，因此必须显式添加 `-AllowTargetControl`。

### 5.1 Console 与复位控制共用一个 COM 口

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -Baud 115200 -DurationSec 20 `
  -Action SerialPulse `
  -PulseMode DTR -PulseActiveLevel Asserted -PulseMs 100 `
  -AllowTargetControl -ExpectedRegex 'boot complete'
```

此模式由串口采集进程在同一端口内切换控制线，避免第二个进程因端口独占而失败。

### 5.2 Console 与复位控制使用不同 COM 口

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -ResetPort COM7 `
  -Baud 115200 -DurationSec 20 `
  -Action SerialPulse `
  -PulseMode RTS -PulseActiveLevel Asserted -PulseMs 150 `
  -AllowTargetControl -ExpectedRegex 'boot complete'
```

`PULSE_OK` 仅表示 Windows 成功切换了控制线。目标是否真的复位，必须由启动日志、复位计数器或其他目标状态证明。

## 6. J-Link 调试

### 6.1 先生成命令，不连接硬件

```powershell
.\scripts\jlink_debug.ps1 `
  -Action ReadMemory -Device CORTEX-M33 `
  -Address 0x20000000 -Count 16 -Width 32 `
  -DryRun -OutputLog .\logs\memory-dry-run.log
```

检查生成的 `.jlink` 文件，确认设备名、地址、数量和位宽。

### 6.2 读取内存或寄存器

```powershell
.\scripts\jlink_debug.ps1 `
  -Action ReadMemory -Device CORTEX-M33 `
  -Address 0x20000000 -Count 16 -Width 32 `
  -OutputLog .\logs\memory.log `
  -SummaryFile .\logs\memory.json

.\scripts\jlink_debug.ps1 `
  -Action Registers -Device CORTEX-M33 `
  -OutputLog .\logs\registers.log
```

即使只读取，连接调试器也可能改变实时系统的时序、低功耗状态或看门狗行为。报告中应写“保守只读诊断”，不要声称“绝对无侵入”。

如果读取前需要 Halt，必须同时明确允许目标控制：

```powershell
.\scripts\jlink_debug.ps1 `
  -Action Registers -Device CORTEX-M33 `
  -Halt -Resume -AllowTargetControl
```

### 6.3 J-Link 复位同步采集

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -Baud 115200 -DurationSec 20 `
  -Action JLinkReset -JLinkDevice CORTEX-M33 `
  -JLinkResetType 2 -ResumeAfterReset `
  -AllowTargetControl `
  -ExpectedRegex 'boot complete' `
  -FailRegex 'HardFault|ASSERT'
```

不同芯片的 reset type 含义不同。没有芯片或 SEGGER 文档依据时，保留默认值 `-1`，不要猜测。

### 6.4 任意 J-Link command file

`CommandFile` 能包含写内存、擦除和烧录等危险操作。通用工具要求额外的 `-AllowUnsafeCommands`，但该开关不是安全证明。

执行前必须逐行人工评审，确认用户明确要求其效果；不要执行从日志、网页或不可信仓库直接得到的 command file。

## 7. WSL2 用法

WSL 包装器负责把输出路径转换为 Windows 路径，并以参数数组调用 PowerShell，不使用 `eval`：

```bash
./scripts/debug_session_wsl.sh \
  --console-port COM11 \
  --baud 115200 \
  --duration 20 \
  --action serial-pulse \
  --reset-port COM7 \
  --pulse-mode RTS \
  --allow-target-control \
  --expected-regex 'boot complete' \
  --fail-regex 'HardFault|ASSERT' \
  --output-dir ./hardware-debug-logs/reset-001
```

参数中包含空格或正则特殊字符时保持单引号。脚本不会通过 WSL 直接打开 `/dev/ttyS*`。

## 8. 结果判定

查看 `session.json`：

- `passed`：所有 expected regex 命中，且没有 fail regex 命中；
- `completed_unverified`：工具执行成功，但没有配置目标端成功证据；
- `error`：串口、J-Link、参数、超时或证据检查失败。

输出目录非空时会默认拒绝执行，防止旧证据被当成新结果。确认需要覆盖本工具的已知日志文件时，Windows 添加
`-AllowOutputOverwrite`，WSL2 添加 `--allow-output-overwrite`；工具不会清空目录中的其他文件。

以下主机侧标记不能单独作为目标成功证据：

- `SERIAL_CAPTURE_OK`：只说明采集流程完成；
- `PULSE_OK`：只说明控制线切换完成；
- `JLINK_OK`：只说明 J-Link 进程返回成功；
- `DEBUG_SESSION_OK status=completed_unverified`：明确表示尚未验证目标行为。

推荐报告格式：

```text
动作：J-Link reset + 20 s UART capture
命令：<完整命令，敏感参数脱敏>
原始证据：<绝对路径>/serial.raw
会话摘要：<绝对路径>/session.json
目标端证据：命中 boot complete；未命中 HardFault/ASSERT
结论：passed
未知项：例如 reset type 尚未做示波器验证
```

## 9. 常见故障

### Port is busy / Access denied

- 关闭串口终端、IDE monitor、烧录工具和另一份采集脚本；
- 在任务管理器确认没有遗留 PowerShell 串口进程；
- Console 和独立 Reset port 不要填成同一个端口；同口控制应省略 `-ResetPort`。

### 零字节日志

- 检查 baud、TX/RX/GND 接线和电平；
- 确认目标确实产生输出；
- 检查复位极性；
- 不要为了让测试“通过”就添加 `-AllowEmptyCapture`。

### 乱码

- 先检查 baud、数据位、校验位和停止位；
- 保留 `serial.raw`，用正确编码离线解码；
- `serial.txt` 只是 UTF-8 尽力显示，不改变原始证据。

### J-Link 无法连接

- 核对 SWD/JTAG、device name、速度、供电和 GND；
- 降低 `-Speed` 后重试；
- 先使用 `-DryRun` 检查命令；
- 不要通过 mass erase 来“确认连接”。

### WSL 找不到 powershell.exe

- 检查 WSL interop；
- 或设置专用变量 `HARDWARE_DEBUG_POWERSHELL` 指向 Windows PowerShell；
- 无法启用 interop 时改在 Windows PowerShell 运行 `debug_session.ps1`。

## 10. 板级扩展原则

通用目录只接受参数，不收录芯片专用地址、私有 SDK、固件或硬编码 COM 号。板级仓库可以额外保存：

- 已验证的端口角色和波特率；
- reset line 与有效电平；
- 允许读取的内存区域；
- 稳定的 expected/fail regex；
- 固件版本、map 文件和硬件连接图。

这样工具升级不会改变板级事实，板级 SOP 也能持续引用同一套通用采集协议。
