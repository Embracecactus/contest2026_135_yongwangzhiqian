# Hardware Adaptation Skills Collection

通用新硬件适配阶段性方法 — 5 个 Skills + 1 个全局规则。

## Skills

| Skill | 功能 | 触发词 |
|---|---|---|
| `stage-restore-prompt` | 阶段恢复提示词自动生成 | `生成恢复提示`、`阶段切换` |
| `hardware-review-gate` | 四维静态评审门禁 | `审查代码`、`review gate` |
| `firmware-gate` | 构建/打包/刷机三道门禁 | `打包`、`构建门禁` |
| `board-verify` | 板端验证（5 种连接模式） | `板端验证`、`检查寄存器` |
| `hardware-context` | 硬件资料索引与查询 | `查找资料`、`SDK 参考` |

## 全局规则

`CLAUDE.md.draft` 包含写入项目 CLAUDE.md 的交互规则：
- 苏格拉底式澄清
- 阶段规划（目标/范围/验收/拆解）
- 主模型/Agent 分工
- 授权门禁
- 已验证 vs 未验证

## 使用方式

将 `skills/hardware-adaptation/` 复制到 `~/.claude/skills/` 即可被 Claude Code 自动识别：

```bash
cp -r skills/hardware-adaptation ~/.claude/skills/
```

或在项目 `.claude/skills/` 中创建软链接：

```bash
mkdir -p .claude/skills
ln -s ../../skills/hardware-adaptation .claude/skills/
```

## 基于

本 Skills 集合提炼自 openvela 2026 竞赛 RV1126B HPMCU 适配经验：
- P1 NSH baseline（硬件移植 + 驱动审查）
- P2-A RPMsg/RPTun（Linux↔HPMCU 核间通信）
- 完整的阶段恢复、静态评审、构建门禁、板端验证流程

openvela/RV1126B 作为 `references/` 深度实例，核心流程用变量抽象，可复用于其他新硬件适配。
