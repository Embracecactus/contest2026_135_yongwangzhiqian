# BK7258 T5AI Core 裸机启动探针

> **适用边界：**硬件目标是 **T5AI Core V1.0.1**，SoC 是 **BK7258**。UART
> 连线、下载路径和实板输出只适用于该硬件目标；启动地址、向量和寄存器等 SoC 级
> 结论还需由当前 `chips/bk7258/` 源码和维护配置确认。

为 BK7258（涂鸦 T5-AI，三核 Cortex-M33）移植 NuttX 做板端去风险验证用的最小
裸探针程序。bootloader 校验 app magic 后会跳到 flash/XIP 逻辑地址 `0x02010000`
的 app `Reset_Handler`；本探针就烧到那里，读当前核号 / `CPUID` / `VTOR` 并经
**UART1** 打印，一次性验证：

- 新写的 linker script（FLASH @ `0x02010000`、RAM @ `0x28000000`、MSP 顶 `0x2809FFFC`）
- 向量表（含 app magic `"BK7236\0\0"`，正好落在文件偏移 `0x100`）
- UART1 early-print 路径（无 libc、无中断）
- bootloader 真正的跳转落点是否是我们的 `Reset_Handler`

这里所有地址与寄存器值与将来的 NuttX BSP 完全共用。

## 文件

| 文件 | 说明 |
|---|---|
| `probe.ld` | 链接脚本：FLASH `0x02010000`/64K，RAM `0x28000000`/640K，`.vectors` 钉在最前 |
| `probe.c` | 向量表 66 项 + `Default_Handler` + `Reset_Handler` + 裸 UART 打印 |
| `Makefile` | `arm-none-eabi-gcc` 构建 `probe.elf` / `probe.bin` |
| `probe.elf` / `probe.bin` | 构建产物（逻辑镜像，链接于 `0x02010000`） |
| `probe.map` | 链接 map（调试用） |
| `probe_crc.bin` | （可选）`bk7258_crc_expand_app.py` 展开后的物理烧录镜像 |

## 构建命令

```bash
cd contest2026_135_yongwangzhiqian/docs/platforms/bk7258/hardware/t5ai-core/probe
make            # 产出 probe.elf / probe.bin
make size       # 打印 section 大小
make clean
```

工具链：`arm-none-eabi-gcc`（`/usr/bin`，实测 10.3.1）。Makefile 顶部可改 `CROSS`
适配其它 Cortex-M 工具链（如 Zephyr SDK 的 `arm-zephyr-eabi-`）。

构建选项：`-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -g -ffreestanding
-Wall -Wextra`，链接 `-T probe.ld -nostdlib -Wl,--gc-sections -Wl,-Map=probe.map`。

## 镜像自检（`probe.bin`）

| 偏移 | 期望字节 | 含义 | 实测 |
|---|---|---|---|
| `0x000` | `fc ff 09 28` | 初始 MSP `0x2809FFFC` 小端 | OK |
| `0x004` | `61 01 01 02` | `Reset_Handler`=`0x02010160`\|Thumb=`0x02010161`（bit0=1） | OK |
| `0x100` | `42 4b 37 32 33 36 00 00` | app magic `"BK7236\0\0"` 小端 | OK |

> 注：`probe.c` 源里 MSP 值严格用规范给定的 `0x2809FFFCu`，其小端字节为
> `fc ff 09 28`（第二个字节是 `ff`）。

## 打包（逻辑镜像 → 物理烧录镜像）

`probe.bin` 是逻辑镜像；BK7258 物理烧录需要按“32 字节数据 + 2 字节 CRC”展开。
打包脚本在 Zephyr 移植仓里：

```bash
python3 /home/lijian/project/TuyaOpen/zephyr-bk7258-port/tools/bk7258_crc_expand_app.py \
    --in  probe.bin \
    --out probe_crc.bin
```

脚本会先校验 `probe.bin` 偏移 `0x100` 处的 magic，再做 CRC 展开，并打印
`app_burn_offset: 0x11000`。本次重建（移除 UART1_CFG 重配后）实测输出：

```
logical_size:           0x26c
physical_size:          0x2a8
app_burn_offset:        0x11000
magic_logical_offset:   0x100
magic_physical_offset:  0x110
combined_magic_offset:  0x11110
magic:                  424b373233360000
```

`probe.bin` = **620 字节（0x26C）**；`probe_crc.bin` = **680 字节（0x2A8）**。
CRC 展开会按 32 字节数据 + 2 字节 CRC 的格式补齐到固定对齐，因此逻辑镜像变小、
物理镜像大小不变（仍 0x2A8，烧录长度沿用此值）。

## 烧录方法 / 命令（板端验证用）

BK7258 没有标准 J-Link/OpenOCD 路线，官方烧录工具是 **BKFIL / bk_loader**
（Beken Flash Image Loader），仅 Windows 下运行，经 **UART**（板上的
`DL_UART0`，进入 ROM 下载模式：按住 BOOT 上电）烧写。`probe_crc.bin` 烧到
**物理偏移 `0x11000`**（不是 `0x10000`）。

板端验证时 custom bootloader 已在板上（物理 0x0），这里只烧 app（物理 `0x11000`）。
`@0x11000-0x2a8` 第二个数是**长度**（`probe_crc.bin` 字节数）不是结束地址。
COM 口与 UNC 路径按实际填，下面是 Windows 模板（占位 `<COM>`、长度用实测值 `0x2a8`）：

```bat
bk_loader.exe download -p <COM> -b 6000000 --uart-type OTHER ^
    --mainBin-multi \\wsl.localhost\Ubuntu-22.04\home\lijian\project\open-vela\contest2026_135_yongwangzhiqian\docs\bk7258-t5ai\probe\probe_crc.bin@0x11000-0x2a8 ^
    --reboot 1 --fast-link 1
```

> 单行写法（参数完全等价）：
> `bk_loader.exe download -p <COM> -b 6000000 --uart-type OTHER --mainBin-multi <probe_crc.bin UNC 路径>@0x11000-0x2a8 --reboot 1 --fast-link 1`

如需把自定义 bootloader + 本探针一起烧（bootloader 在 `0x0`，本探针在 `0x11000`），
参考 `zephyr-bk7258-port/docs/12-custom-bootloader.md` 的 `--mainBin-multi` 多段写法。

GUI 方式：`BKFIL.exe` 选串口、选 `probe_crc.bin`、起始地址按工具约定（来源
`bk_idk/include/soc/bk7258/reg_base.h` 的 `SOC_FLASH_DATA_BASE`）。

## UART1 配置：继承 custom bootloader（探针不重配）

`Reset_Handler` **不再写 `UART1_CFG`**（即不再写 `0x45830010`）。理由：板端实测
（`zephyr-bk7258-port/docs/12-custom-bootloader.md` §6/§10、Zephyr
`soc_reset_hook` 实测）显示板上跑的是自制 custom bootloader `bk7236_min_bl.S`，它在
跳 app 前已完整初始化 UART1——GPIO0/1 pinmux、clock gate `0x44010030` bit10、WDT
喂狗、`UART1` `global_ctrl` 与 `config`——实测 `UART1_CFG = 0x00003719`（`clk_div =
0x37`），并非 SDK 推导的 `0xE0`。探针如果再强行写 `0x0000E01B`，就会和板上实际的
UART 时钟分频冲突，看不到任何输出。

正确做法就是 Zephyr `soc_reset_hook` 实测可打印的做法：**不动 `config`，直接写
`fifo_port` (`0x4583001C`)** 发字节，靠轮询 `fifo_stat` (`0x45830018`) bit20 等待
FIFO 可写。`probe.c` 里 `uart_putc` 现在做的就是这件事，且 `UART1_CFG` 宏定义仍
保留（作为寄存器地图，便于将来 NuttX BSP 引用），只是 `Reset_Handler` 不再引用它。

## 预期 UART1 输出（波特率 = bootloader 打印 BL/APP?/OK/JMP 那个波特率）

复位后 UART1 会沿用 bootloader 已配好的波特率（也就是上位机看到 `BL ... / APP? /
OK / JMP` 那条 banner 时的那个波特率），连续打印下列 4 行后死循环：

```
BK7258 PROBE
core=0x????????
cpuid=0x????????
vtor=0x02010000
HALT
```

紧凑写法（同一组字段）：`BK7258 PROBE / core=0x???????? / cpuid=0x???????? /
vtor=0x02010000 / HALT`。判读：

- `core=` 来自 `0x20000000` 的软件约定字。**custom bootloader 不写
  `cpu0_set_core_id()`，所以这个字可能是任意未初始化值（含非 0 的随机值）**，看到
  `0x00000000` 固然好，看到别的值并不代表跑错了核。
  **真正证明本探针跑在 CPU0 上的证据是“探针能执行 + `vtor=0x02010000`”**——能执行
  说明 bootloader 把它当 CPU0 的 app 跳进来了，`VTOR` 与我们写入的 `0x02010000`
  一致说明我们的向量表确实被装载并接管。`core=` 字段只作辅助参考。
- `cpuid=` Cortex-M33 的 `SCB->CPUID` 典型值为 `0x410FC241`（Implementer=0x41 ARM，
  Variant=0x0，Constant=0xF，PartNo=0xC24=Cortex-M33，Revision=0x1）。
- `vtor=` 必须为 `0x02010000`（与 `Reset_Handler` 里写入的 VTOR 一致），证明我们的
  向量表确实被指向了。若为别的值，说明 VTOR 写失败或跳转落点不对。

`vtor` 异常 → 排查 linker script / 向量表 / bootloader 跳转。能看到完整 4 行（无论
`core=` 是不是 0）→ 新写的 linker / 向量表（含 magic）/ UART1 early-print（继承
bootloader 配置）/ bootloader 跳转落点全部正确，可以放心进 NuttX BSP 移植。

## 与规范的偏离

- MSP 小端字节是 `fc ff 09 28`（`0x2809FFFC`）；规范自检表里写的 “`FC 9F 09 28`”
  是笔误（`9F` 应为 `FF`），源码与镜像均按规范给定的 MSP 数值 `0x2809FFFC` 不变。
- 其余地址 / 寄存器值 / 向量表布局 / magic 全部严格照规范。
