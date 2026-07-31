# Claude / Codex 硬件调试执行规范

本文件约束 AI 如何调用本目录工具。完整参数和故障处理见 [SOP.zh-CN.md](SOP.zh-CN.md)。

## 1. 每次任务的固定流程

1. **确认环境**：判断当前在 Windows 还是 WSL2；确认脚本绝对路径和日志目录。
2. **确认板级事实**：Console COM、baud、目标型号，以及任务涉及的 reset port、DTR/RTS 极性、J-Link device/address。不能可靠推断时询问用户。
3. **分类动作**：按下表判断现有授权是否足够。不要把“诊断问题”扩大成“复位、停核或烧录”。
4. **先建基线**：列端口，做短时纯串口采集；需要 J-Link 时先运行 `-DryRun` 检查命令。
5. **先采集后触发**：复位场景必须用 `debug_session`；看到 `SERIAL_READY` 后才允许目标控制动作发生。
6. **验证目标证据**：优先设置 `ExpectedRegex` 和 `FailRegex`；必要时结合启动计数、generation、寄存器或其他目标端事实。
7. **归档和报告**：给出执行命令、`serial.raw`、`session.json`、命中的证据和未知项。不得只报告 `PULSE_OK` 或 `JLINK_OK`。

## 2. 授权矩阵

| 类别 | 动作 | AI 处理 |
| --- | --- | --- |
| 主机只读 | 列举 COM、查看帮助、J-Link `-DryRun`、读取现有日志 | 可直接执行 |
| 目标保守诊断 | 纯 UART 采集、无 Halt 的 J-Link memory/register read | 在用户要求诊断且目标明确时执行；说明 debugger attach 可能影响时序 |
| 可逆目标控制 | DTR/RTS pulse、reset、halt、resume | 仅当用户明确把该动作放入范围；传入 `-AllowTargetControl` |
| 高风险/破坏性 | flash、erase、loadfile、内存写、Option Byte、Fuse、安全状态修改 | 本 skill 不执行；转到芯片专用、经评审的 SOP |
| 任意命令文件 | J-Link `CommandFile` | 仅执行用户明确要求且逐行评审过的文件；否则拒绝传入 `-AllowUnsafeCommands` |

“复位后抓启动日志”明确包含 reset；“查为什么没有输出”不自动包含 reset；“看看寄存器”不自动包含 Halt。

## 3. 端口与进程规则

- Windows 是 COM 口唯一所有者。WSL2 通过 `powershell.exe` 调用 Windows 脚本。
- 一个 COM 口同一时间只由一个进程打开。关闭 PuTTY、MobaXterm、IDE monitor 和烧录器的串口窗口。
- 同一 USB-UART 同时承担 console 和 reset line 时，不设置 `ResetPort`，由 `serial_capture.ps1` 内联切换 DTR/RTS。
- Console 与 reset control 是两个适配器时，设置不同的 `ConsolePort` 和 `ResetPort`。
- 不确定线路有效电平时，不试遍所有组合；先查原理图或请用户确认。

## 4. 命令选择

Windows 纯采集或同步会话：

```powershell
.\scripts\debug_session.ps1 -ConsolePort COM11 -Baud 115200 `
  -DurationSec 20 -Action ManualReset -ExpectedRegex 'boot complete'
```

WSL2 使用包装器，不在 Linux 中直接占用映射串口：

```bash
./scripts/debug_session_wsl.sh \
  --console-port COM11 --baud 115200 --duration 20 \
  --action manual-reset --expected-regex 'boot complete'
```

J-Link 先审查 dry run：

```powershell
.\scripts\jlink_debug.ps1 -Action ReadMemory -Device CORTEX-M33 `
  -Address 0x20000000 -Count 16 -Width 32 -DryRun
```

只使用从芯片文档、linker map、当前源码或用户给定证据确认的地址。不要从相似芯片猜地址。

## 5. 证据和结论规则

- `serial.raw` 是不可改写的原始证据；解码、筛选或脱敏应生成新文件。
- `serial.txt` 是 UTF-8 尽力解码，乱码不意味着原始数据损坏。
- `passed` 表示配置的正反向证据检查通过，不表示所有系统功能正确。
- `completed_unverified` 必须按“动作完成但目标结果未验证”报告，不能改写成成功。
- 空日志默认失败。只有任务明确预期目标静默时才使用 `AllowEmptyCapture`，并解释依据。
- 串口打开失败时，不要循环复位目标；先解决端口占用、驱动或参数问题。
- J-Link 连接失败时，不要用 erase、unlock 或写寄存器探测连接。

## 6. AI 最终报告模板

```text
环境：Windows PowerShell / WSL2 -> Windows PowerShell
动作：<capture/manual reset/serial pulse/J-Link read/reset>
授权依据：<用户原始要求；无目标控制则写“不涉及”>
命令：<完整命令，敏感内容脱敏>
原始日志：<绝对路径>/serial.raw
结构化结果：<绝对路径>/session.json
目标端证据：<命中/未命中的表达式、计数器或状态>
结论：passed / completed_unverified / error
未知项：<复位极性、地址来源、调试器时序影响等>
```

## 7. 可直接给 Claude/Codex 的示例提示词

纯观察：

```text
使用 windows-hardware-debug skill，在 WSL2 中通过 Windows COM11 以
115200 baud 采集 20 秒原始串口数据。不要复位、停核或写目标。保存原始
日志和 JSON，并告诉我是否收到任何字节。
```

复位同步：

```text
使用 windows-hardware-debug skill，先打开 COM11/115200 串口采集，再用
J-Link 对 CORTEX-M33 执行一次复位并继续运行。我授权这一次 reset，不授权
flash/erase/write。要求日志命中 "boot complete" 且不出现 "HardFault|ASSERT"，
最后给出 session.json 路径和证据。
```

只读诊断：

```text
使用 windows-hardware-debug skill，先 dry-run，再从 0x20000000 读取 16 个
32-bit word。不要 halt、reset、resume 或写内存。保存生成的 J-Link 命令和日志，
并说明 debugger attach 对实时性的潜在影响。
```
