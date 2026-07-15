# 阶段 2 恢复提示词 — RV1126B 板端启动验证

> **用途：** `/clear` 后粘贴此内容以恢复上下文，继续阶段 2 工作。

---

## 背景

我在做 RV1126B HPMCU 的 openvela/NuttX 移植。阶段 1（最小闭环构建修复）已完成，当前目标是让板端通过串口输出 NSH 提示符。

**平台参数：**
- RV1126B HPMCU（RISC-V）
- 串口：UART5M0, 1500000, 8N1
- RAM：0x48c02000, 大小 0x3a000（232KB）

**项目结构：**
- 工作目录：`$CONTEST/`
- 这是 contest overlay，通过 manifest 链接到 openvela 构建树
- 构建命令：
  ```bash
  cd "$WORKSPACE"
  export PATH=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin:$PATH
  ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
  ```

## 阶段 1 已完成

构建已通过，最终产物：
- `nuttx` ELF 32-bit RISC-V
- Entry point: 0x48c02000
- Size: 86300 bytes (36.33% of 232KB RAM)

修复的关键问题（共 14 项，详见 `2026-07-12-rv1126b-openvela-porting.md`）：
- scripts/Make.defs、chip/Make.defs、src/Makefile：构建系统修复
- rv1126b_head.S：_start→__start, trap frame 修正
- rv1126b_irq_dispatch.c：通过 riscv_doirq 分发
- rv1126b_irq.c/serial.c/timerisr.c/atomic.c/bringup.c：驱动修复与补充
- defconfig/rv1126b_config.h：配置修正

## 阶段 2 目标：板端验证

通过 UART5M0 看到 NSH 输出（即 NuttX Shell 启动提示符 `nsh>`）。

### 需要用户提供的信息

要继续阶段 2，**请用户提供以下信息**：

1. **HPMCU 固件加载命令/脚本** — 如何将 nuttx ELF 加载到 RV1126B HPMCU 上运行？是否有官方工具（如 downloader、启动脚本、OpenOCD 配置）？
2. **hpmcu_start.bin 来源** — HPMCU 启动固件从哪里获取？是否包含在 SDK 中？
3. **镜像打包流程** — 是否需要类似 `mkimage.sh` 的脚本将 nuttx ELF 打包成可烧录格式？具体参数是什么？（如 load address、entry point、image header 格式）
4. **烧录/调试硬件** — SWD, JTAG, UART boot, USB bulk? 板子上有什么可用的烧录方式？

### 可选任务：打包脚本

如果 SDK 中存在 `mkimage.sh` 或类似镜像打包工具，可能需要：
- 参考 SDK 的 `mkimage.sh` 确认打包参数（--load-addr 0x48c02000, --entry 0x48c02000, HPMCU header magic 等）
- 适配生成可直接通过 HPMCU bootloader 加载的镜像

### 子任务清单（待信息完备后执行）

- [ ] 获取 HPMCU 加载命令/脚本和 hpmcu_start.bin
- [ ] 将 nuttx ELF 打包为可烧录镜像
- [ ] 通过 UART 观察板端输出
- [ ] 根据串口输出分析启动失败原因（如卡在哪一阶段）
- [ ] 可能需要修改：clock 配置、UART pinmux、INTMUX 路由、内存 mapping
- [ ] 修复后迭代直到 NSH 启动

## 协作规则

- 你（Opus）只做规划和审核，不要直接写代码
- 查代码必须先用 CodeGraph：`projectPath="$WORKSPACE"`
- 具体修改和执行交给子代理（Sonnet/Haiku）
- 所有改动只在 `contest2026_135_yongwangzhiqian/` overlay 内
- 每完成一个子任务，更新 `docs/ai-worklog/2026-07-12-rv1126b-openvela-porting.md`
- **遇到需要硬件/SDK 信息时，暂停并向用户索取，不要凭空猜测**

## 下一步

请用户提供 HPMCU 固件加载命令/脚本和 hpmcu_start.bin 来源，然后基于这些信息规划镜像打包和烧录流程。
