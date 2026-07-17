# RV1126B Restore Prompt Examples

Real-world restore prompts from the RV1126B openvela 2026 contest adaptation.
Used as templates for `stage-restore-prompt`.

## Pattern Summary

Each restore prompt follows this structure:

1. **路径约定** — `export WORKSPACE/CONTEST/SDK/OUT/FW`
2. **严格交互规则** — 苏格拉底澄清、不主动加载 skill、不使用 Workflow、主/Agent 分工
3. **修改边界** — 只改 `$CONTEST`、外层只读、SDK 只读预检、授权门禁
4. **当前状态** — Git HEAD、分支、已修改/新增文件列表
5. **已验证/未验证** — 板端观察 vs 静态审查 vs 未验证
6. **关键文件** — 当前阶段涉及的代码/配置/文档
7. **构建产物 hash** — nuttx.bin/amp.img/update.img 的 SHA-256
8. **下一阶段目标** — 明确的验收标准

## Key Rules Extracted

- 路径变量：`$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`（不写个人绝对路径）
- Hash 完整性：SHA-256 必须 64 位完整字符串
- 已验证定义：只有板端观察到的行为才算已验证
- 构建 ≠ 板端：构建通过不等于板端成功
- 授权门禁：构建/打包/刷机/PR 需明确授权

## Example Topics Covered

| Prompt File | Stage | Key Content |
|---|---|---|
| `next-stage-prompt-2026-07-15.md` | P0 环境搭建 | 路径约定、CodeGraph 规则、初始状态 |
| `next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md` | P0 适配继续 | 苏格拉底规则、提交规则、Git 状态 |
| `next-stage-prompt-2026-07-15-rv1126b-p0-p1-fix.md` | P0/P1 修复 | 评审结果、修复范围、授权边界 |
| `next-stage-prompt-2026-07-16-rv1126b-p1-merged-ready-for-p2.md` | P1→P2 过渡 | P1 已合并、P2 规划、hash 基线 |
| `next-stage-prompt-2026-07-16-rv1126b-p1-packaged-awaiting-board-test.md` | P1 打包 | SDK 链路、hash 验证、刷机门禁 |
| `p2-rpmsg-next-stage-prompt.md` | P2-A 已验证 | 双向 mailbox 证据、Route B 目标 |

## Template Skeleton

```text
我们正在 openvela 2026 竞赛工作区继续工作。当前阶段：$STAGE_NAME。

═══════════════════════════════════════════
路径约定
═══════════════════════════════════════════

  export WORKSPACE=/absolute/path/to/open-vela
  export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
  export SDK=/absolute/path/to/rv1126b-sdk
  export OUT="$SDK/output"
  export FW="$OUT/firmware"

═══════════════════════════════════════════
严格主模型 / 执行 Agent 分工
═══════════════════════════════════════════

主模型只负责：
- 苏格拉底式需求澄清
- 架构 / 实施规划
- 任务拆解、验收标准
- 审核和综合 Agent 证据
- 与用户沟通决策

全部交给普通 Agent 的工作：
- 搜索、codegraph sync、CodeGraph 查询
- 文件阅读、diff 检查、实际代码评审
- 具体文档生产、文件读写、代码修改
- 构建测试、hash 和证据收集

═══════════════════════════════════════════
当前状态
═══════════════════════════════════════════

$CURRENT_STATE

═══════════════════════════════════════════
已验证 / 未验证
═══════════════════════════════════════════

已验证：
$VERIFIED_ITEMS

未验证：
$UNVERIFIED_ITEMS

═══════════════════════════════════════════
下一阶段目标
═══════════════════════════════════════════

$NEXT_GOAL

验收标准：
$ACCEPTANCE_CRITERIA
```
