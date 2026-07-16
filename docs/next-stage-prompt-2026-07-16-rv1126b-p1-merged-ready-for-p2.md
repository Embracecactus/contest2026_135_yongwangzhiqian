# 下一阶段恢复提示词：RV1126B P1 已合并，准备 P2 规划

在上下文重置后使用本文档继续推进 RV1126B HPMCU openvela / NuttX contest overlay。

```text
我们正在 openvela 2026 竞赛工作区继续工作。当前状态：P1 NSH baseline 已合并到官方
仓库；代码收敛 + 模板归一化 + 驱动审查 + 板端验证全部通过。下一阶段是规划 P2 工作
范围或进入最终项目 README 阶段。

严格交互规则：

- 使用苏格拉底式沟通：如果范围或意图不清楚，先停下来问我，不要自行扩展范围。
- 不要主动加载 skills，除非我明确要求。尤其不要自动运行 build / flash / SDK packaging 相关 skill。
- 本项目不要使用 Workflow。
- 主模型保持在规划和复核角色；重复检查、机械验证可在实际需要且范围明确时委托普通 Agent。
- 如果主模型可能超时，可使用 Sonnet/Haiku Agent 继续已明确的机械执行或验证，避免中断任务。
- 只修改 `$CONTEST` 团队仓库。不要直接修改外层官方 checkout，例如 `$WORKSPACE/nuttx/`、
  `$WORKSPACE/apps/`、`$WORKSPACE/packages/`、`$WORKSPACE/vendor/`、`$WORKSPACE/frameworks/`。
- SDK 下的 build/package 产物仅在我明确授权时覆盖；任何覆盖前先检查目标并列出旧 hash。
- 使用 CodeGraph 前先运行 `codegraph sync`；然后调用 CodeGraph 时使用 `projectPath: "$WORKSPACE"`。
  如果 sync 或 CodeGraph 不可用，先说明 fallback，再进行手工读取/搜索。
- 未经确认，不要运行长构建、SDK 打包、刷机、push、创建 PR、删除、reset 或覆盖性操作。
- 文档中不要写入个人绝对路径。使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。
- README 在我明确说进入最终项目 README 阶段前，应尽量保持接近官方模板。

路径约定：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$OUT/firmware"
```

实际 `$SDK` 绝对路径由用户在上一会话中提供，但没有写入仓库文档。上下文恢复后若环境变量未设置，
先请用户重新提供或确认；不要自行猜测路径。

当前 Git / PR 上下文：

- 团队仓库：`$CONTEST`
- 分支：`submit-rv1126b-nsh-baseline`
- 跟踪远端：`fork/submit-rv1126b-nsh-baseline`
- PR 已合并（Rebase and merge）到官方 `openvela/contest2026_135_yongwangzhiqian`
- 合并后 HEAD 需通过 `git log` 确认（用户执行了 Rebase and merge，commit hash 可能已变）
- Working tree clean；ahead 0 after merge

会话开始只读检查：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" diff --check
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

P1 已完成的全部工作（已合并）：

1. MTIME tick 跟随 NuttX 配置：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - 使用 `<nuttx/clock.h>` 的 `TICK_PER_SEC`，不再固定除以 100。
   - 增加编译期 guard，防止 tick rate 非法或 compare step 截断为 0。

2. GPLL 失败路径可见：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_clockconfig.c`
   - GPLL 未锁定时通过 `riscv_lowputc()` 输出：`WARNING: GPLL not locked`。
   - 修正 GPLL lock 注释：`GPLL_CON[2]` offset `0x28`、bit 10。

3. Console 符号收敛：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_serial.c`
   - `g_console_port` 改为 `static`。

4. Team 135 template 归一化：
   - Kconfig symbol：`LVX_USE_DEMO_CONTEST2026_135_HELLO_APP`
   - APPDIR target：`packages/demos/contest2026_135_hello_app`
   - QuickApp package：`com.openvela.contest2026.team135`
   - QuickApp mapping：`packages/apps/contest2026_135_hello_quickapp`

5. 文档一致性：
   - UART5 IRQ namespace 已澄清（INTMUX source 61 → `RV1126B_IRQ_UART5 = RISCV_IRQ_EXT + 61`）
   - `board/contest_board/README.md` 状态已提升为 board-verified

6. 驱动代码审查：
   - 双轮交叉验证（Pattern 全面 + 深层安全）
   - 评分：100/100 PASS，设计健康度 A
   - 仅 1 个 Low（`irq_attach` 失败静默返回）未修复，不阻塞

P1 验证证据（已归档）：

- 板端串口 transcript（RKDevTool.exe 烧写）：
  - `WARNING: GPLL not locked` 出现
  - `ps`: IDLE + nsh_main 正常
  - `uname -a`: `NuttX 0.0.0 e02f581e23 Jul 16 2026 02:59:57 risc-v rv1126b_evb`
  - UART RX/TX 交互正常

P1 产物 hash（用于未来对比基线）：

| 产物 | sha256 |
| --- | --- |
| `nuttx.bin` | `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00` |
| `amp.img` | `585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9` |

当前不可声称（除非用户明确推进）：

- CMake 构建等价
- DCache 已启用/已验证
- RPMsg / Linux A-core 到 HPMCU 通信已完成
- full `update.img` 打包或刷写已通过
- GPIO3 地址已确认（`rv1126b_memorymap.h` 和 `rv1126b_gpio.h` 仍冲突）
- 最终项目 README 已完成

可能的 P2 方向（需用户明确选择）：

A. RPMsg / A-core ↔ HPMCU 通信
B. GPIO driver 开发（需先核实 GPIO3 地址）
C. CMake 构建验证
D. DCache 启用
E. 更多 NuttX 子系统测试（procfs, work_queue 等）
F. 最终项目 README 编写
G. 其他用户自定义目标
```

## 本上下文窗口变更摘要

### 合并状态

PR 已通过 Rebase and merge 合并到官方仓库。本地分支与远端应已同步。

### 已通过检查

```text
classic Make build：exit code 0
AMP FIT packaging (mkimage)：exit code 0
Board runtime：NSH prompt, ps, uname -a, UART RX/TX, GPLL warning
Driver code review：100/100 PASS, design health A
AI log validation：95 files / 26689 events / ALL OK
PR merged via Rebase and merge
```

### 未 commit 未 push

无。Working tree 应为 clean。

### 待用户决定

下一步 P2 方向选择，或进入最终项目 README 阶段。
