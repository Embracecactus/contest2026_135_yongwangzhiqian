# 阶段 1 恢复提示词 — RV1126B 最小闭环构建修复

> **用途：** `/clear` 后粘贴此内容以恢复上下文，继续阶段 1 工作。

---

## 背景

我在做 RV1126B HPMCU 的 openvela/NuttX 移植，目标是最小闭环：UART 能输出、内核能启动。

**平台参数：**
- RV1126B HPMCU（RISC-V）
- 串口：UART5M0, 1500000, 8N1
- RAM：0x48c02000, 大小 0x3a000

**项目结构：**
- 工作目录：`$CONTEST/`
- 这是 contest overlay，通过 manifest 链接到 openvela 构建树
- 构建命令：`cd "$WORKSPACE" && ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8`

## 阶段 1 任务（进行中）

需要完成以下修复才能成功构建：

1. **`rv1126b_head.S` trap 入口** — 确保 trap/中断入口调用 `riscv_dispatch_irq`（标准 NuttX RISC-V 中断分发），不要直接调用具体 C handler
2. **`rv1126b_irq_dispatch.c`** — 中断分发函数签名必须返回 `uint32_t *regs`（或等效指针），这是 NuttX RISC-V 的约定
3. **`defconfig` 验证** — 检查 `board/contest_board/configs/nsh/defconfig` 中 UART、RAM 地址、中断号等是否匹配 RV1126B 硬件
4. **构建验证** — 上述修复后执行构建，确认无编译/链接错误
5. **板端验证** — 烧录后通过串口观察 NSH 启动输出

## 协作规则

- 你（Opus）只做规划和审核，不要直接写代码
- 查代码必须先用 CodeGraph：`projectPath="$WORKSPACE"`
- 具体修改和执行交给子代理（Sonnet/Haiku）
- 所有改动只在 `contest2026_135_yongwangzhiqian/` overlay 内
- 每完成一个子任务，更新 `docs/ai-worklog/2026-07-12-rv1126b-openvela-porting.md`

## 上次进展

- [x] 创建 ai-worklog 文档结构
- [ ] 定位并修复 `rv1126b_head.S` 的 trap/IRQ 入口
- [ ] 定位并修复 `rv1126b_irq_dispatch.c` 的返回值
- [ ] 验证 defconfig 配置
- [ ] 执行构建并修复编译错误
- [ ] 打包固件

## 下一步

请先用 CodeGraph 查找 `rv1126b_head` 和 `riscv_dispatch_irq` 相关符号，确认当前实现和需要修改的位置，然后委派子代理执行修改。
