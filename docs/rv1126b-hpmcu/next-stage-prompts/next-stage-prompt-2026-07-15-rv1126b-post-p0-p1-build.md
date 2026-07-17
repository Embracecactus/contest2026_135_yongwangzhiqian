# 下一阶段恢复提示词：RV1126B P0/P1 BSP 修复后构建验证

在上下文重置后使用本文档继续推进 RV1126B HPMCU openvela / NuttX 适配。本文件同时记录上一阶段已经完成的 P0/P1 代码修改。

```text
我们正在 openvela 2026 竞赛工作区继续工作。当前目标：在 2026-07-15 P0/P1 BSP blocker 修复已经完成的基础上，验证并最终整理 RV1126B HPMCU openvela / NuttX contest overlay。

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
- P0/P1 修复阶段前已知最新推送 commit：`9b5f438 logs: sync latest session`
- 既有 PR 目标：
  - base repository：`open-vela/contest2026_135_yongwangzhiqian`
  - base branch：`dev-ai-contest-2026`
  - compare repository：`Embracecactus/contest2026_135_yongwangzhiqian`
  - compare branch：`submit-rv1126b-nsh-baseline`

会话开始检查：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" diff --stat -- board/contest_board/
git -C "$CONTEST" status --short -- logs docs
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

如果发现有新增 logs，提交或推送前先问我。

优先阅读文档：

```text
$CONTEST/docs/rv1126b-hpmcu/next-stage-prompts/next-stage-prompt-2026-07-15-rv1126b-post-p0-p1-build.md
$CONTEST/docs/review/2026-07-15-rv1126b-board-targeted-code-review.md
```

P0/P1 修复后的当前代码状态：

- P0/P1 代码修复已经实现，但尚未提交。
- 已修改的 tracked 文件：
  - `$CONTEST/board/contest_board/chip/CMakeLists.txt`
  - `$CONTEST/board/contest_board/chip/include/irq.h`
  - `$CONTEST/board/contest_board/chip/rv1126b_atomic.c`
  - `$CONTEST/board/contest_board/chip/rv1126b_head.S`
  - `$CONTEST/board/contest_board/chip/rv1126b_irq.c`
  - `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c`
  - `$CONTEST/board/contest_board/chip/rv1126b_serial.c`
  - `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
  - `$CONTEST/board/contest_board/scripts/ld.script`
- 最终整理前还存在这些 untracked docs：
  - `$CONTEST/docs/rv1126b-hpmcu/next-stage-prompts/next-stage-prompt-2026-07-15-rv1126b-p0-p1-fix.md`
  - `$CONTEST/docs/review/2026-07-15-rv1126b-board-targeted-code-review.md`
  - 本恢复文档

上一阶段已完成的代码修改：

1. 修复 atomic builtins：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_atomic.c`
   - 对每个 32-bit `__atomic_*_4` read-modify-write 序列增加 `up_irq_save()` / `up_irq_restore()` 和 `UP_DMB()`。
   - 保留 `__atomic_compare_exchange_4()` 失败时的语义：在 IRQ 关闭期间把观察到的当前值写回 `*expected`。

2. 修复 INTMUX IRQ namespace：
   - 文件：`$CONTEST/board/contest_board/chip/include/irq.h`
   - 新增 INTMUX source 到 NuttX IRQ、NuttX IRQ 到 source 的转换宏。
   - 保持约定：`NuttX IRQ = RISCV_IRQ_EXT + intmux_source_id`。
   - 预留 INTMUX source 0，因为它在 M-mode 下会与聚合槽 `RISCV_IRQ_EXT` / `RISCV_IRQ_MEXT` 冲突。
   - 新增 `RV1126B_UART5_INTMUX_SOURCE` 和 `RV1126B_IRQ_UART5`。

3. 修复 INTMUX controller 和 UART IRQ 使用：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_irq.c`
   - 底层 INTMUX enable/disable 现在接收 raw INTMUX source id，并进行合法性检查。
   - `up_enable_irq()` / `up_disable_irq()` 现在分别处理 `RISCV_IRQ_MTIMER`、`RISCV_IRQ_MEXT` 和有效 INTMUX IRQ。
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c`
   - INTMUX 扫描现在返回 raw source id，屏蔽预留的 source 0，并通过 `riscv_doirq(RV1126B_INTMUX_SOURCE_TO_IRQ(source), regs)` 分发外部中断。
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_serial.c`
   - UART5 现在 attach/enable `RV1126B_IRQ_UART5`，不再把 raw source `61` 当作 NuttX IRQ 使用。

4. 修复 timer dispatch：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c`
   - Machine timer trap 现在通过 `riscv_doirq(RISCV_IRQ_MTIMER, regs)` 分发，而不是直接调用 timer tick 例程。
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - Timer 例程现在通过 `irq_attach(RISCV_IRQ_MTIMER, ...)` 注册为标准 IRQ handler。
   - `up_timer_initialize()` 通过 `up_enable_irq(RISCV_IRQ_MTIMER)` 使能 timer。

5. 修复 RV32 MTIME / MTIMECMP split access：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - MTIME 读取改为 high-low-high retry 序列，避免低 32 位翻转导致不一致。
   - MTIMECMP 写入改为在 `up_irq_save()` / `up_irq_restore()` 保护下按 high=`UINT32_MAX`、low、final-high 的顺序写入，随后执行 `UP_DSB()`。

6. 修复 trap frame 与 stack alignment：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_head.S`
   - 复用 common RISC-V `<arch/irq.h>` 中的 canonical register offset 常量。
   - trap stack frame 分配大小按 `STACKFRAME_ALIGN` 向上取整，同时保持 canonical `REG_*` offset 不变。
   - 初始 SP 在调用 C 前按 `STACKFRAME_ALIGN` 向下对齐。
   - 一轮只读 subagent review 发现并已修复真实问题：`t1/x6` 在保存前被 clobber。当前代码先保存 `x6`，再使用 `t1` 计算 pre-trap SP。

7. 修复 linker alignment：
   - 文件：`$CONTEST/board/contest_board/scripts/ld.script`
   - `_ebss` 和 `_sheap` 改为 16-byte 对齐。

8. 对 CMake source-set parity 做 fail-fast gating：
   - 文件：`$CONTEST/board/contest_board/chip/CMakeLists.txt`
   - RV1126B CMake 路径现在会 fail fast，并明确说明当前 contest baseline 只支持 classic Make source set。
   - 原因：外层 `$WORKSPACE/nuttx/arch/risc-v/src/common/CMakeLists.txt` 会在 chip CMake 之后加入 common `riscv_mtimer.c`，而修改官方 checkout 超出当前范围。

已完成的验证：

```bash
git -C "$CONTEST" diff --check origin/dev-ai-contest-2026...HEAD
git -C "$CONTEST" diff --check -- board/contest_board/
git -C "$CONTEST" diff --stat -- board/contest_board/
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

已知结果：

- `diff --check`：无 whitespace/style 错误。
- P0/P1 修复阶段的 board diff stat：9 个文件变更，约 329 行新增、187 行删除。
- log validation：93 个文件、26036 个 events，结果为 `ALL OK`。
- 修复后普通只读 subagent review 已完成：没有新增 likely blocker 或 compile error。
- 修改后已经运行过 `codegraph sync`。

上一阶段之前的板端证据仍然有效，但注意它发生在本轮 P0/P1 代码修改之前：

1. 2026-07-15 15:58 PROCFS / `ps` recheck 通过：
   - `nuttx.bin` / `$OUT/rtt.bin` hash：`b428e9d2a259addc72574f2080c2038b9a948befa0fab29a48180e1e27619b43`
   - `nuttx.bin` / `$OUT/rtt.bin` size：98464 bytes
   - `$FW/amp.img` hash：`d390e0f738507ed58d59770e3a2dd9ee236f399f95134920dcc8336f69982835`
   - `$FW/amp.img` size：103424 bytes
   - 板端 NSH 已验证：`ps`、`uname -a`、UART RX/TX。
2. 正确替换 `$OUT/rtt.bin` 时必须保留 symlink，并复制到实际 target：

```bash
RTT_TARGET="$(readlink -f "$OUT/rtt.bin")"
cp -av "$WORKSPACE/nuttx/nuttx.bin" "$RTT_TARGET"
```

除非重新验证，否则不要声称以下事项：

- P0/P1 代码修改已经构建成功；
- P0/P1 修改后的板端 runtime 已验证；
- full `update.img` flashing 已通过；
- RPMsg / Linux A-core 到 HPMCU 通信已完成；
- CMake 等价；
- DCache 已启用/已验证。

推荐下一步：

1. 重新打开并 review 当前 9 个已修改 board 文件的 diff。
2. 问我是否运行 classic Make build。没有确认前不要运行。
3. 如果我确认，从 `$WORKSPACE` 运行：

```bash
cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

4. 如果 build 失败，只修 `$CONTEST` 文件。如果修复需要改 `$WORKSPACE/nuttx/`，停止并问我跨仓库 PR 处理方式。
5. 如果 build 通过，问我要准备 SDK/板端验证说明，还是授权你运行 packaging/flash 步骤。
6. Board/SDK 步骤默认应作为说明提供给我执行，除非我明确授权你运行：
   - 保留 `$OUT/rtt.bin` symlink，并替换 `readlink -f "$OUT/rtt.bin"` 的目标文件。
   - 重新打包 AMP image。
   - 只刷写目标 AMP partition，除非我明确要求 full `update.img` validation。
   - 重新测试 NSH prompt、UART RX/TX、`help`、`uname -a`、`ps`。
7. 构建/板端证据完成后，更新 docs/logs，并在 commit/push 前询问我。若决定提交，优先拆成小提交：
   - atomic + IRQ namespace；
   - timer dispatch + MTIME split access；
   - trap/linker alignment + CMake gating；
   - docs/log sync（如需要）。
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
- 将 INTMUX external IRQ 规范化到 NuttX IRQ namespace，并预留 source 0，避免与 `RISCV_IRQ_MEXT` 聚合槽冲突。
- 将 machine timer interrupt 路由到 `riscv_doirq(RISCV_IRQ_MTIMER, regs)`，并用 `irq_attach()` 注册 timer ISR。
- 实现安全的 RV32 split MTIME / MTIMECMP 访问。
- 恢复 startup/trap-to-C 边界的 16-byte stack alignment，并修复 review 发现的 `x6/t1` 保存顺序问题。
- 将 `_ebss` 和 `_sheap` 对齐到 16 bytes。
- 因无法只在 `$CONTEST` 内干净修复 Make/CMake parity，RV1126B CMake 路径改为 fail-fast。

### 已运行检查

```text
codegraph sync
git diff --check origin/dev-ai-contest-2026...HEAD
git diff --check -- board/contest_board/
git diff --stat -- board/contest_board/
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
普通 subagent 对 P0/P1 修复做只读 review
```

结果：最终只读 review 没有新增 findings；轻量检查未发现失败。
