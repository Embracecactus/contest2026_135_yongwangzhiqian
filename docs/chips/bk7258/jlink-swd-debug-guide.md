# BK7258 chip 层 J-Link/SWD 调试指南

> 适用环境：WSL2 (编译) + Windows (JLink GDB Server)
> 硬件：JLink EDU/Plus + BK7258 (Cortex-M33)
>
> 本文只定义 BK7258/Cortex-M33 共用的 SWD、fault 和寄存器调试方法。实际 SWDIO、
> SWCLK、RESET、GND、VTref 接线、COM 口与复位极性属于板级事实，应从对应板卡原理图
> 和验证记录取得。自动采集优先使用
> `tools/windows-hardware-debug/`。
>
> 旧版文档使用 `bl_crc.bin + nuttx_crc.bin` direct 镜像举例；当前维护交付已改为
> board-owned BL1 → pinned NuttX MCUboot BL2 → signed same-slot CP/AP。不得照抄旧地址
> 进行写 Flash，本指南不授权下载、擦除、Option Byte 或 OTP/eFuse 操作。

---

## 1. 环境准备

### WSL2 侧

```bash
sudo apt-get install gdb-multiarch
```

### Windows 侧

JLink Software Pack 需已安装（默认路径 `C:\Program Files\SEGGER\JLink\`）。

---

## 2. 启动 JLink GDB Server

**Windows PowerShell**：

```powershell
# 先杀掉可能残留的 JLink 进程
Get-Process | Where-Object {$_.Name -like "*JLink*"} | Stop-Process -Force
Start-Sleep -Seconds 2

# 启动 GDB Server（Cortex-M33 通用设备名）
& "C:\Program Files\SEGGER\JLink\JLinkGDBServerCL.exe" -device Cortex-M33 -if SWD -speed 4000 -port 2331
```

成功输出：
```
Listening on TCP/IP port 2331
Connecting to target...
Connected to target
```

**注意**：
- 如果 `BK7258` 不在 JLink 设备列表，用 `Cortex-M33` 通用名
- 如果端口被占（`Failed to open listener port`），先杀旧进程或换端口
- 如果 `Connection refused`，检查 JLink 驱动和 USB 连接

---

## 3. WSL2 连接 Windows JLink

WSL2 和 Windows 宿主之间的网络是隔离的。连接方式：

### 方法 A：PowerShell 内直接运行 WSL GDB（推荐）

```powershell
wsl gdb-multiarch '<CP_ELF>' \
  -ex "target remote localhost:2331" \
  -ex "monitor reset halt" \
  -ex "break bk7258_hardfault_handler" \
  -ex "continue"
```

### 方法 B：拷 ELF 到 Windows

```bash
# WSL 侧
win_desktop=$(powershell.exe -NoProfile -Command \
  '[Environment]::GetFolderPath("Desktop")' | tr -d '\r')
cp '<CP_ELF>' "$(wslpath "$win_desktop")/nuttx.elf"
```

```powershell
# Windows PowerShell
cd $env:USERPROFILE\Desktop
& "C:\Program Files\SEGGER\JLink\arm-none-eabi-gdb.exe" nuttx.elf
```

GDB 内：
```gdb
target remote localhost:2331
monitor reset halt
break bk7258_hardfault_handler
continue
```

### 方法 C：WSL2 内连接 Windows 宿主 IP

```bash
# 查看 Windows 宿主 IP
WINIP=$(cat /etc/resolv.conf | grep nameserver | awk '{print $2}')
# 如果 $WINIP 不通，试：
# ip route show default | awk '{print $3}'
# 或：ipconfig.exe | grep -i "IPv4" | head -1

gdb-multiarch '<CP_ELF>'
```

GDB 内：
```gdb
target remote $WINIP:2331
monitor reset halt
break bk7258_hardfault_handler
continue
```

**注意**：如果 `Connection timed out`，可能是 Windows 防火墙挡了。用方法 A 最可靠。

---

## 4. Hard Fault 调试

### 4.1 捕获 Hard Fault

```gdb
# 在 hard fault handler 设断点
break bk7258_hardfault_handler
continue
# 板子启动 → 触发 fault → GDB 停住
```

### 4.2 读取 Fault 寄存器

```gdb
# CFSR (Configurable Fault Status Register) — 告诉你 fault 类型
print/x *(uint32_t *)0xE000ED28

# HFSR (Hard Fault Status Register)
print/x *(uint32_t *)0xE000ED2C

# MMFAR (MemManage Fault Address) — 如果 CFSR bit[7]=1
print/x *(uint32_t *)0xE000ED34

# BFAR (Bus Fault Address) — 如果 CFSR bit[15]=1
print/x *(uint32_t *)0xE000ED38
```

### 4.3 读取崩溃时的寄存器上下文

```gdb
# EXC_RETURN 值（LR 在异常模式下的值）
print/x $lr

# 栈指针
print/x $sp

# 异常帧（压栈的 R0-R3, R12, LR, PC, xPSR）
x/8x $sp
```

异常帧布局（Cortex-M33 标准）：
```
$sp+0x00: R0
$sp+0x04: R1
$sp+0x08: R2
$sp+0x0C: R3
$sp+0x10: R12
$sp+0x14: LR (返回地址)
$sp+0x18: PC (崩溃指令地址)  ← 关键！
$sp+0x1C: xPSR
```

### 4.4 定位崩溃代码

```gdb
# 用 stacked PC 反汇编
info line *0xXXXXXXXX          # 替换为 stacked PC
list *0xXXXXXXXX               # 显示源码

# 或者用 addr2line（WSL 终端）
arm-none-eabi-addr2line -e '<CP_ELF>' -f 0xXXXXXXXX
```

### 4.5 CFSR 位域解读

| 位 | 名称 | 含义 | 常见原因 |
|---|---|---|---|
| [0] | IACCVIOL | 指令访问违规 | 从不可执行区域取指 |
| [1] | DACCVIOL | 数据访问违规 | 访问未映射/保护内存 |
| [3] | MUNSTKERR | 出栈时 MemManage | 栈损坏 |
| [4] | MSTKERR | 入栈时 MemManage | 栈溢出 |
| [7] | MMARVALID | MMFAR 有效 | 读 MMFAR 得到 fault 地址 |
| [8] | IBUSERR | 指令总线错误 | 取指时总线故障 |
| [9] | PRECISERR | 精确数据总线错误 | 数据访问时总线故障 |
| [10] | IMPRECISERR | 不精确数据总线错误 | 异步总线故障 |
| [11] | UNSTKERR | 出栈时 BusFault | 栈损坏 |
| [12] | STKERR | 入栈时 BusFault | 栈溢出 |
| [15] | BFARVALID | BFAR 有效 | 读 BFAR 得到 fault 地址 |
| [16] | UNDEFINSTR | 未定义指令 | 跳转到数据区/错误地址 |
| [24] | NOCP | 无协处理器 | FPU 未启用但用了浮点指令 |
| [25] | INVPC | 无效 PC | 非法 EXC_RETURN |

---

## 5. 常用 GDB 命令速查

```gdb
# 连接/断开
target remote localhost:2331
monitor reset halt
disconnect

# 断点
break bk7258_hardfault_handler
break board_app_initialize
break bk7258_wdt_initialize
info break
delete 1

# 执行
continue
step
next
finish                    # 执行到当前函数返回

# 查看
info registers            # 所有寄存器
print/x $pc               # 当前 PC
print/x $lr               # 链接寄存器
print/x $sp               # 栈指针
x/16x $sp                 # 栈内容
x/8i $pc                  # 当前 PC 处的指令

# 内存
x/16x 0xE000ED00          # SCB 寄存器块
x/1x 0xE000ED28           # CFSR
x/1x 0xE000ED2C           # HFSR
x/1x 0xE000ED34           # MMFAR
x/1x 0xE000ED38           # BFAR
x/1x 0xE000E010           # SysTick CSR
x/1x 0xE000E014           # SysTick RVR
x/1x 0xE000E018           # SysTick CVR

# NuttX 特定
print g_system_tick        # 系统 tick 计数
info threads               # NuttX 线程列表（如果有 RTOS 插件）
```

---

## 6. BK7258 特有调试要点

### 6.1 启动链

```text
BootROM（芯片内部固化）
  → board-owned BL1（DPLL/早期时钟、BL2 manifest 验签和回退）
    → pinned NuttX MCUboot BL2（同槽 CP/AP image 验签与选择）
      → signed CP NuttX
        → nx_start()
          → irq/clock/up/drivers/board initialize
          → ROMFS rc.sysinit → final-init → rcS（按 profile）
      → signed AP NuttX（由 CP/AP control 和 profile 决定是否启动）
```

当前物理地址、slot 大小和下载边界只以 manifest 与权威分区 CSV 为准。历史 direct
镜像中的 `bl_crc.bin @ 0`、`nuttx_crc.bin @ 0x11000` 不能用于当前签名镜像判读。

### 6.2 常见 Hard Fault 原因

| 现象 | 可能原因 | 排查方法 |
|---|---|---|
| 启动后立即 HF | 栈溢出、未定义指令、FPU 未启用 | 读 CFSR + stacked PC |
| NSH 后几秒 HF | WDT 超时复位（bootloader 武装的 AON/APB WDT） | 检查 `bk_aon_wdt_stop()` 是否被调 |
| `bk_wdt_start` 后 HF | timer ISR 未接上（`bk_timer_driver_init` 未调） | 确认 `bk_timer_driver_init()` 在 `bk_wdt_start` 前 |
| 编译后行为变化 | 增量编译未重编关键 .o | 用统一 CLI `--clean` 重建；对最终 role ELF 执行 `file`/`nm` |

### 6.3 WDT 调试

```gdb
# 检查 AON WDT 状态
x/1x 0x44000600           # AON_WDT_CTRL

# 检查 APB WDT 状态
x/1x 0x44800010           # APB_WDT_CTRL
x/1x 0x44800004           # APB_WDT_STATUS
```

### 6.4 SysTick 调试

```gdb
x/1x 0xE000E010           # SYST_CSR (ENABLE|TICKINT|CLKSOURCE)
x/1x 0xE000E014           # SYST_RVR (reload value)
x/1x 0xE000E018           # SYST_CVR (current value, 应在递减)
```

---

## 7. 构建验证检查清单

每次编译后、下载前先通过统一工具 clean build，并从输出的 layout/role identity 目录
选择本次最终 ELF；不要使用工作区根部可能过期的 `nuttx/nuttx`：

```bash
# 1. CP_ELF 必须指向本次 out/bk7258/.../roles/.../cmake/nuttx
file "$CP_ELF" | grep ARM

# 2. 确认关键符号存在
arm-none-eabi-nm "$CP_ELF" | grep -E "board_app_initialize|bk7258_wdt_initialize|arm_lowputc"

# 3. 记录最终 config/ELF 的哈希与 identity
sha256sum "$CP_CONFIG" "$CP_ELF"

# 4. 确认 resolved config，而不是只看 defconfig
grep -E "CONFIG_ARCH_CHIP_BK7258|CONFIG_ARCH_CHIP_CUSTOM_DIR" "$CP_CONFIG"
```

签名交付还必须依次通过 package、public trust 和 flash contract 校验。下载命令、保留
区和禁止区由板级
[build/package/hardware evidence SOP](../../platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md)
定义；J-Link 调试指南本身不提供写入授权。
