# 下一阶段恢复提示词：RV1126B PR 提交后

在上下文重置后使用本文档继续推进 RV1126B HPMCU openvela / NuttX contest overlay。

```text
我们正在 openvela 2026 竞赛工作区继续工作。当前目标：PR 已提交，
等待 CLA 签署和 review，同时准备后续验证和优化。

严格交互规则：

- 使用苏格拉底式沟通：如果范围或意图不清楚，先停下来问我，不要自行扩展范围。
- 不要主动加载 skills，除非我明确要求。尤其不要自动运行 build / flash / SDK packaging 相关 skill。
- 本项目不要使用 Workflow。
- 主模型保持在规划和复核角色；重复检查、机械验证可在实际需要且范围明确时委托普通 Agent。
- 只修改 `$CONTEST` 团队仓库。不要直接修改外层官方 checkout，例如 `$WORKSPACE/nuttx/`、`$WORKSPACE/apps/`、`$WORKSPACE/packages/`、`$WORKSPACE/vendor/`、`$WORKSPACE/frameworks/`。
- 使用 CodeGraph 前先运行 `codegraph sync`；然后调用 CodeGraph 时使用 `projectPath: "$WORKSPACE"`。如果 sync 或 CodeGraph 不可用，先说明 fallback，再进行手工读取/搜索。
- 未经确认，不要运行长构建、SDK 打包、刷机、push、创建 PR、删除、reset 或覆盖性操作。
- 文档中不要写入个人绝对路径。使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。
- README 在我明确说进入最终项目 README 阶段前，应尽量保持接近官方模板。

路径约定：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"
```

当前仓库上下文：

- 团队仓库：`$CONTEST`
- 分支：`submit-rv1126b-nsh-baseline`
- 跟踪远端：`fork/submit-rv1126b-nsh-baseline`
- PR 已提交：
  - base repository：`open-vela/contest2026_135_yongwangzhiqian`
  - base branch：`dev-ai-contest-2026`
  - compare repository：`Embracecactus/contest2026_135_yongwangzhiqian`
  - compare branch：`submit-rv1126b-nsh-baseline`
- PR 状态：等待 CLA 签署（邮箱 `15588296118@163.com`，需在
  https://www.openvela.com/#/community/cla 签署后评论 `/check-cla`）
- 最新 commit：`57c6738 logs: sync latest session`

会话开始检查：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" diff --stat -- board/contest_board/
git -C "$CONTEST" status --short -- logs docs
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

如果发现有新增 logs，提交或推送前先问我。

已完成的工作：

1. P0/P1 BSP 修复（全部完成并验证）：
   - atomic shims：up_irq_save/restore + UP_DMB 保护
   - INTMUX IRQ namespace：NuttX IRQ = RISCV_IRQ_EXT + source，source 0 保留
   - INTMUX controller：enable/disable RMW 加 IRQ 保护
   - timer dispatch：通过 riscv_doirq(RISCV_IRQ_MTIMER) 分发
   - MTIME/MTIMECMP：安全的 RV32 分割寄存器访问
   - trap frame：16 字节对齐、标准 REG_* 偏移、x6/t1 保存顺序
   - mstatus 恢复：移至 mret 前最后执行，防止嵌套陷阱
   - linker：KEEP(.text.start)、_ebss/_sheap 16 字节对齐
   - CMake：fail-fast（contest 基线仅支持经典 Make）

2. 代码审查（双轮审查，全部 findings 已修复）：
   - INTMUX 分发循环加 256 次上限
   - CAS 统一单一出口 restore
   - UART IER RMW 加 IRQ 保护
   - timer irq_attach 返回值检查
   - NuttX 大括号风格修正
   - IPIC 使能值、MEIE 前置条件注释

3. 板端验证（P0/P1 修复后）：
   - NSH prompt 正常工作
   - help、uname -a、ps 命令已验证
   - UART RX/TX 正常
   - amp.img 打包方式：$SDK/rtos/bsp/rockchip/tools/mkimage -f amp.its -E -p 0xe00 $FW/amp.img

4. 文档和日志：
   - 代码审查报告：docs/review/2026-07-15-rv1126b-board-targeted-code-review.md
   - 恢复提示词：docs/next-stage-prompt-2026-07-15-*.md
   - AI 日志：93 个会话文件，26069 个事件，已验证

已提交的 commit（按时间顺序）：

```text
57c6738 logs: sync latest session
044e491 docs: add P0/P1 fix docs, code review report, and sync session logs
dc9b8ed board: fix RV1126B BSP P0/P1 blockers and code review findings
9b5f438 logs: sync latest session
a002df8 logs: backfill AI sessions
cf7ac6d docs: add submission follow-up restore prompt
b41a694 enable ps in RV1126B NSH baseline
bea5ff0 document verified RV1126B NSH baseline
ec43ebb RV1126B HPMCU NuttX port: cleanup, documentation, and AI logs
```

除非重新验证，否则不要声称以下事项：

- CMake 等价；
- DCache 已启用/已验证；
- RPMsg / Linux A-core 到 HPMCU 通信已完成；
- full `update.img` flashing 已通过。

推荐下一步：

1. 签署 CLA（https://www.openvela.com/#/community/cla），然后在 PR 评论区
   输入 `/check-cla` 重新检查。
2. 等待 PR review 反馈，根据 reviewer 意见修改代码。
3. 如果 review 通过，准备最终提交材料：
   - 确认所有 commit 已 push
   - 确认日志验证通过
   - 确认文档完整
4. 如果需要进一步验证：
   - 从 `$WORKSPACE` 运行 build：./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
   - 按正确方式打包 amp.img：$SDK/rtos/bsp/rockchip/tools/mkimage -f amp.its -E -p 0xe00 $FW/amp.img
   - 刷写 AMP 分区验证 NSH
5. 如果 reviewer 要求修改：
   - 只修改 `$CONTEST` 文件
   - 如果需要改 `$WORKSPACE/nuttx/`，停下来讨论跨仓库处理方式
```

## 本上下文窗口修改记录

本节记录写入本文档前已经完成的工作。

### P0/P1 修复阶段修改过的文件

```text
$CONTEST/board/contest_board/chip/CMakeLists.txt
$CONTEST/board/contest_board/chip/include/irq.h
$CONTEST/board/contest_board/chip/rv1126b_atomic.c
$CONTEST/board/contest_board/chip/rv1126b_head.S
$CONTEST/board/contest_board/chip/rv1126b_irq.c
$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c
$CONTEST/board/contest_board/chip/rv1126b_serial.c
$CONTEST/board/contest_board/chip/rv1126b_timerisr.c
$CONTEST/board/contest_board/scripts/ld.script
```

### 修改摘要

- 使用 IRQ masking 和 barriers 修复 unsafe custom 32-bit GCC atomic shims。
- 将 INTMUX external IRQ 规范化到 NuttX IRQ namespace，并预留 source 0。
- 将 machine timer interrupt 路由到 riscv_doirq(RISCV_IRQ_MTIMER)，并用 irq_attach() 注册 timer ISR。
- 实现安全的 RV32 split MTIME / MTIMECMP 访问。
- 恢复 startup/trap-to-C 边界的 16-byte stack alignment。
- trap exit 将 mstatus 恢复移至 mret 前最后执行。
- 将 _ebss 和 _sheap 对齐到 16 bytes。
- RV1126B CMake 路径改为 fail-fast。

### 代码审查修复

- INTMUX dispatch 循环加 256 次上限。
- CAS 统一单一出口 restore。
- UART IER RMW 加 up_irq_save/restore 保护。
- timer irq_attach 返回值检查。
- NuttX 大括号风格修正。
- IPIC 使能值、MEIE 前置条件注释。

### 关键经验

- amp.img 打包必须用 $SDK/rtos/bsp/rockchip/tools/mkimage（不是 u-boot mkimage），
  且需要 -E（加密）和 -p 0xe00（padding）参数。
- 调试二分法时发现的"无输出"问题根因是打包方式错误，不是代码问题。
- Build 非确定性（每次 hash 不同），不能靠 hash 对比判断二进制是否相同。

### 已运行检查

```text
codegraph sync
git diff --check origin/dev-ai-contest-2026...HEAD
git diff --check -- board/contest_board/
git diff --stat -- board/contest_board/
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
双轮代码审查（52 Pattern + 深层安全分析）
```

结果：全部通过，所有 High findings 已修复。
```
