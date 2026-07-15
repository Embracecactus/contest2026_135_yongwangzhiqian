> [!WARNING]
> **Historical / superseded research.** This report preserves the early investigation and decision trail; it is not current implementation guidance. The verified UART5 M0/IPIC route, classic-Make policy, and current test boundary are in the [canonical RV1126B NSH port guide](rv1126b-nsh-port.md). For observed board behavior, use the immutable [2026-07-14 baseline evidence](verification/2026-07-14-rv1126b-nsh-baseline.md). In particular, the body’s UART4, polling, placeholder, and pending-status claims are historical rather than current.

# RV1126B 适配 open-vela 调研报告

## 1. 背景

open-vela 大赛选题：将 open-vela (NuttX) 操作系统适配到 Rockchip RV1126B 的 HPMCU (高性能 MCU) 核心上。

**关键前提：**

- 用户使用自有的 RV1126B 开发板（非正点原子 ATK 板），但基础 SDK 相同。
- SDK 路径：`$SDK`
- SDK 中无 Zephyr RTOS，仅有 RT-Thread RTOS 的 BSP。
- Zephyr upstream 适配是赛后独立方向，不纳入本次比赛主线。

---

## 2. 总体结论

**比赛主线策略：open-vela / NuttX BSP 适配 RV1126B HPMCU**

| 维度 | 结论 |
|------|------|
| 比赛主线 | open-vela/NuttX 适配 RV1126B HPMCU |
| 硬件参考 | SDK 内 RT-Thread BSP（`rtos/bsp/rockchip/rv1126b-mcu/`） |
| Zephyr | 官方无 RV1126B 支持；upstream 需完整适配，属赛后独立方向 |
| SDK 基座 | 基于 ATK DL RV1126B Linux 6.1 SDK，用户板硬件一致 |

---

## 3. 当前 contest overlay 已有内容

仓库路径：`contest2026_135_yongwangzhiqian/`

### 3.1 Manifest 与链接

- `contest2026_135_yongwangzhiqian.xml`：包含 `openvela.xml`，通过 `<linkfile>` 将团队目录映射到 openvela 构建树。
  - `app/hello_app` -> `packages/demos/contest2026_135_hello_app`
  - `quickapp/hello_quickapp` -> `packages/apps/contest2026_135_hello_quickapp`
  - `board/contest_board` -> `vendor/openvela/boards/contest2026_135_board`

### 3.2 RV1126B HPMCU BSP 入口

`board/contest_board/chip/` 下已有完整的 BSP 源文件：

| 文件 | 功能 |
|------|------|
| `rv1126b_head.S` | 启动汇编入口（`_start`），栈初始化、BSS 清零、C 入口跳转 |
| `rv1126b_start.c` | C 语言启动初始化（`rv1126b_board_initialize` 等） |
| `rv1126b_clockconfig.c` | 时钟配置（GPLL、APB 等） |
| `rv1126b_irq.c` | 中断控制器初始化 |
| `rv1126b_irq_dispatch.c` | 中断分发逻辑 |
| `rv1126b_timerisr.c` | 定时器中断（Systick） |
| `rv1126b_serial.c` | UART 串口驱动 |
| `rv1126b_lowputc.c` | 底层串口输出（early console） |
| `rv1126b_allocateheap.c` | 堆内存分配 |
| `rv1126b_config.h` | 板级配置宏 |
| `rv1126b_memorymap.h` | 内存映射定义 |
| `rv1126b.h` | 总头文件 |
| `chip.h` | 芯片抽象头文件 |

### 3.3 硬件寄存器头文件

`board/contest_board/chip/hardware/`：

- `rv1126b_cru.h` — CRU (Clock Reset Unit) 寄存器
- `rv1126b_gpio.h` — GPIO 寄存器
- `rv1126b_intmux.h` — 中断复用器寄存器
- `rv1126b_memorymap.h` — 外设基地址映射
- `rv1126b_timer.h` — 定时器寄存器
- `rv1126b_uart.h` — UART 寄存器

### 3.4 链接脚本

`board/contest_board/scripts/ld.script`：NuttX RISC-V 链接脚本。

### 3.5 Defconfig

`board/contest_board/configs/nsh/defconfig`：NSH 最小配置。

### 3.6 构建集成

- `board/contest_board/chip/CMakeLists.txt`
- `board/contest_board/chip/Make.defs`
- `board/contest_board/chip/Kconfig`

---

## 4. 仍未清理的 Placeholder

以下位置仍包含模板占位符，构建前必须替换：

| 占位符 | 出现位置 | 说明 |
|--------|---------|------|
| `contest2026_000` | Kconfig 符号、defconfig、构建路径 | 团队编号需改为 `135` |
| `team000` | 包名、目录名 | 团队标识需更新 |
| `your-github-login` | `logs/` 目录下示例文件 | 需替换为实际 GitHub 用户名 |
| `board_boot.c` | 部分引用仍指向旧文件名 | 当前实际文件为 `rv1126b_start.c` |
| `CONFIG_*_000_*` | defconfig 中的配置项 | 需与团队 135 对齐 |

---

## 5. SDK 关键事实

### 5.1 RT-Thread BSP 路径

```
$SDK/rtos/bsp/rockchip/rv1126b-mcu/
```

目录结构：

```
rv1126b-mcu/
├── Image/
│   └── amp.its           # AMP 镜像打包描述
├── Kconfig
├── Makefile / SConscript / SConstruct
├── applications/          # 应用示例
├── board/                 # 板级适配
├── cpu/                   # CPU 架构支持
├── drivers/               # 驱动实现
├── hal_conf.h             # HAL 配置
├── link.lds               # 链接脚本（关键参考）
├── mkimage.sh             # 镜像打包脚本
├── rkbin/                 # Rockchip 二进制工具
├── rtconfig.h             # RT-Thread 配置
└── rtconfig.py            # 构建配置脚本
```

### 5.2 构建方式

RT-Thread 使用 SCons 构建系统（`SConstruct` + `SConscript`）。打包通过 `mkimage.sh` 脚本完成。

### 5.3 link.lds 内存布局（关键参考）

```ld
OUTPUT_ARCH( "riscv" )
ENTRY(_start)

MEMORY {
    /* mcu VMA on ddr space:
       A53.bin: 0x48c00000, 8KB
       mcu:     0x48c02000, 232KB (0x3a000)
       mcu_log: 0x48c0c000, 16KB */
    RAM (rwx)         : ORIGIN = 0x48c02000, LENGTH = 0x3a000
    LINUX_RPMSG (rxw) : ORIGIN = 0x48c3c000, LENGTH = 0x20000
}
```

**关键参数：**

- HPMCU 运行地址：`0x48c02000`
- HPMCU 可用内存：232KB（`0x3a000`）
- Linux RPMsg 共享内存：`0x48c3c000`，128KB（`0x20000`）
- A53 固件区：`0x48c00000`，8KB
- HPMCU 日志区：`0x48c0c000`，16KB

### 5.4 amp.its / amp_mcu.its

- `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/amp.its`：AMP 镜像打包描述文件
- `$SDK/device/rockchip/.chips/rv1126b/amp_mcu.its`：MCU AMP 镜像配置

### 5.5 hpmcu_start.bin

打包流程生成 `hpmcu_start.bin`，由 Linux 端 AMP 框架加载到 HPMCU 运行地址。

### 5.6 UART4 1.5Mbaud

SDK 默认 HPMCU 调试串口为 **UART4**，波特率 **1500000 (1.5Mbps)**。这是 Rockchip MCU 的典型配置。

### 5.7 HPMCU 396MHz

HPMCU 运行频率：**396MHz**，由 CRU 从 GPLL 分频得到。

### 5.8 Rockchip AMP 启动链路

```
DDR 加载 A53.bin
    → A53 (Linux) 启动
    → AMP 框架加载 hpmcu_start.bin 到 0x48c02000
    → HPMCU 开始执行 _start
```

DTS 参考：`$SDK/kernel-6.1/arch/arm64/boot/dts/rockchip/rv1126b-amp.dtsi`

### 5.9 RPMsg / Mailbox / OpenAMP 线索

- SDK 中存在 RPMsg 共享内存区域定义（`LINUX_RPMSG` 段）
- Linux DTS 中有 AMP 相关配置
- RT-Thread BSP 中可能包含 OpenAMP/RPMsg 组件（需进一步确认 `components/` 目录）
- 这是 Linux 与 HPMCU 之间 IPC 的基础机制

---

## 6. Zephyr 调研结论

### 6.1 官方支持状态

- Zephyr 官方 **不支持 RV1126B**。
- 已有的 Rockchip RISC-V 支持集中在：RV1106、RV1103、Luckfox Pico Mini/Max。
- 这些 SoC 与 RV1126B 差异较大（不同 CPU 核、不同外设布局）。

### 6.2 Zephyr Upstream 适配需求

若要将 RV1126B 推入 Zephyr upstream，需要：

| 工作项 | 说明 |
|--------|------|
| SoC 定义 | `dts/riscv/rockchip/rv1126b*.dtsi` |
| Board 定义 | `boards/rockchip/rv1126b_*` |
| Driver 适配 | UART、GPIO、Timer、CRU 等 |
| 文档 | 板级文档、开发指南 |
| 测试 | 硬件测试验证 |

### 6.3 结论

Zephyr upstream 适配是独立的、赛后的长期工作方向，不纳入本次比赛主线。

---

## 7. 最高优先级风险

### 7.1 UART0 vs UART4 配置冲突

- **问题**：当前 `rv1126b_serial.c` / `rv1126b_lowputc.c` 可能默认使用 UART0，但 SDK 约定 HPMCU 调试串口为 UART4。
- **影响**：如果 UART 配置不正确，无法看到任何串口输出，调试完全受阻。
- **参考**：SDK RT-Thread BSP `drivers/` 中的 UART 驱动使用 UART4。

### 7.2 GRF_SYS 基址矛盾

- **问题**：`hardware/rv1126b_memorymap.h` 中 GRF_SYS 基地址可能与 SDK 定义不一致。
- **影响**：时钟、GPIO 复用等寄存器操作全部出错。
- **对策**：必须与 SDK `hal_conf.h` 或 Rockchip TRM 逐一核对。

### 7.3 GPLL 初始化策略冲突

- **问题**：`rv1126b_clockconfig.c` 中 GPLL 初始化逻辑可能与 SDK 方案不同（是否需要等待 PLL 锁定、分频系数等）。
- **影响**：时钟频率不正确导致 UART 波特率偏差、定时器不准。
- **参考**：SDK RT-Thread BSP `board/` 目录中的时钟初始化代码。

### 7.4 SCR1/IPIC 私有中断路径

- **问题**：RV1126B HPMCU 使用 SCR1 CPU 核心，其中断控制器为 IPIC（Internal Programmable Interrupt Controller），不同于标准 RISC-V PLIC/CLINT。
- **影响**：当前 NuttX RISC-V 中断框架可能需要适配 IPIC。
- **参考**：`hardware/rv1126b_intmux.h` 和 SDK RT-Thread 的中断处理代码。

### 7.5 hpmcu_start.bin / 打包链路

- **问题**：open-vela 编译输出的 ELF/BIN 如何打包成 AMP 框架可加载的 `hpmcu_start.bin`？
- **影响**：即使编译成功，也无法实际部署运行。
- **参考**：SDK `mkimage.sh` 和 `amp.its`。

### 7.6 RPMsg 共享内存 Size 不一致

- **问题**：`link.lds` 中 `LINUX_RPMSG` 段大小（128KB）需与 Linux 端 DTS 配置完全一致。
- **影响**：不一致会导致 Linux 与 HPMCU IPC 通信失败或内存踩踏。

### 7.7 模板占位

- **问题**：`contest2026_000`、`team000` 等占位符未清理。
- **影响**：构建路径错误、Kconfig 解析失败。

---

## 8. 推荐路线

### Phase 0：清理模板（立即）

- 替换所有 `contest2026_000` -> `contest2026_135`
- 替换所有 `team000` -> `team135`（或按实际团队名）
- 替换 `your-github-login`
- 验证 manifest linkfile 路径正确
- 确认 `build.sh` 可找到 board config

### Phase 1：对齐 SDK 最小启动参数（核心）

- **UART**：将串口从 UART0 切换到 UART4，波特率设为 1500000
- **内存映射**：将 `ld.script` 的 ORIGIN/LENGTH 与 SDK `link.lds` 对齐
  - HPMCU 基地址：`0x48c02000`
  - 可用大小：`0x3a000`（232KB）
- **时钟**：对齐 GPLL 配置，确保 CPU 频率 396MHz
- **中断**：适配 SCR1 IPIC（非标准 PLIC/CLINT）

### Phase 2：打包和启动链路验证（关键）

- 研究 SDK `mkimage.sh` 打包流程
- 将 open-vela 输出转换为 AMP 可加载格式
- 验证 Linux AMP 框架能正确加载 HPMCU 固件
- 确认 HPMCU 从 `_start` 开始执行

### Phase 3：最小 open-vela demo（里程碑）

- 成功启动 NuttX 内核
- NSH shell 通过 UART4 输出
- 基本 tick 定时器工作
- （可选）RPMsg 与 Linux 端通信

### 赛后：Zephyr Upstream

- 基于已验证的硬件参数，独立推进 Zephyr upstream 适配
- 需要完整的 SoC/DTS/Board/Driver/Doc 贡献

---

## 9. 需要用户确认的硬件信息

以下信息需要用户根据实际硬件确认，直接影响 BSP 正确性：

| 序号 | 确认项 | 说明 |
|------|--------|------|
| 1 | **UART 引脚** | UART4 的 TX/RX 对应哪些引脚？是否与 ATK 板一致？ |
| 2 | **Sensor** | 摄像头 sensor 型号？（如 SC3338/OV4689 等） |
| 3 | **I2C 外设** | 哪些 I2C 总线上挂了什么设备？ |
| 4 | **GPIO 使用** | 有哪些 GPIO 被用于特殊功能（LED、按键、使能脚等）？ |
| 5 | **电源管理** | PMIC 型号？是否需要额外电源域配置？ |
| 6 | **存储** | eMMC/SD/NAND 型号和容量？ |
| 7 | **显示** | 是否有 LCD/MIPI 屏？型号和分辨率？ |
| 8 | **DDR** | DDR 型号、容量、频率？影响内存布局 |
| 9 | **共享内存** | Linux 与 HPMCU 共享内存区域是否需要调整？ |
| 10 | **启动模式** | 从什么介质启动？（SD/eMMC/SPI NOR） |
| 11 | **网络** | 以太网/WiFi 是否需要在 HPMCU 侧使用？ |
| 12 | **调试接口** | JTAG/SWD 调试口是否引出？ |

---

## 10. 下一步建议

### 立即行动

1. **只读差异审计 + 补丁计划**：对比当前 BSP 源码与 SDK RT-Thread BSP 的关键参数差异，生成补丁清单。不做修改，只出报告。
2. **清理 placeholder**：替换 `contest2026_000`、`team000`、`your-github-login` 等。

### 短期（Phase 1）

3. **UART 对齐**：修改 `rv1126b_serial.c` 和 `rv1126b_lowputc.c`，从 UART0 切换到 UART4，波特率 1500000。
4. **Linker 脚本对齐**：修改 `ld.script` 内存区域定义，与 SDK `link.lds` 一致。
5. **更新 README**：记录适配进展和已知问题。

### 谨慎推进（Phase 2）

6. **暂不贸然修改** clock/GRF/interrupt 代码——先完成审计，理解 SDK 的初始化时序后再改。
7. **研究打包链路**：分析 `mkimage.sh`，确定如何将 NuttX ELF 转换为 AMP 可加载 bin。

---

## 附录：关键文件路径索引

### Contest 仓库内

```
contest2026_135_yongwangzhiqian/
├── contest2026_135_yongwangzhiqian.xml          # Manifest
├── board/contest_board/
│   ├── chip/
│   │   ├── rv1126b_head.S                       # 启动汇编
│   │   ├── rv1126b_start.c                      # C 启动
│   │   ├── rv1126b_clockconfig.c                # 时钟配置
│   │   ├── rv1126b_irq.c                        # 中断初始化
│   │   ├── rv1126b_irq_dispatch.c               # 中断分发
│   │   ├── rv1126b_timerisr.c                   # 定时器
│   │   ├── rv1126b_serial.c                     # UART 驱动
│   │   ├── rv1126b_lowputc.c                    # 底层串口
│   │   ├── rv1126b_allocateheap.c               # 堆分配
│   │   ├── rv1126b_config.h                     # 配置宏
│   │   ├── rv1126b_memorymap.h                  # 内存映射
│   │   ├── rv1126b.h                            # 总头文件
│   │   ├── chip.h                               # 芯片抽象
│   │   └── hardware/
│   │       ├── rv1126b_cru.h                    # CRU 寄存器
│   │       ├── rv1126b_gpio.h                   # GPIO 寄存器
│   │       ├── rv1126b_intmux.h                 # 中断复用
│   │       ├── rv1126b_memorymap.h              # 外设基地址
│   │       ├── rv1126b_timer.h                  # 定时器寄存器
│   │       └── rv1126b_uart.h                   # UART 寄存器
│   ├── scripts/
│   │   ├── ld.script                            # NuttX 链接脚本
│   │   └── Make.defs
│   └── configs/nsh/
│       └── defconfig                            # NSH 配置
├── app/hello_app/                               # 示例 NuttX 应用
├── quickapp/hello_quickapp/                     # 示例 QuickApp
└── logs/                                        # AI Coding 日志
```

### SDK 内

```
$SDK=/absolute/path/to/rv1126b-sdk

$SDK/rtos/bsp/rockchip/rv1126b-mcu/              # RT-Thread BSP（硬件参考）
├── link.lds                                     # 内存布局参考
├── mkimage.sh                                   # 打包脚本
├── Image/amp.its                                # AMP 镜像描述
├── drivers/                                     # 驱动参考
├── board/                                       # 板级初始化参考
└── cpu/                                         # CPU 架构参考

$SDK/device/rockchip/.chips/rv1126b/
├── amp_mcu.its                                  # MCU AMP 镜像配置
└── parameter*.txt                               # 启动参数

$SDK/kernel-6.1/arch/arm64/boot/dts/rockchip/
└── rv1126b-amp.dtsi                             # AMP DTS 配置
```
