---
name: stage-restore-prompt
description: >
  Generate a self-contained stage-restore prompt for hardware adaptation sessions.
  Captures git status, .config, build artifacts (hash/size), verified/unverified state,
  and authorization gates into a Markdown fenced text block that can be pasted into a
  new session to restore context. Use when: switching stages, before /clear, generating
  next-stage prompt, restore prompt, 阶段切换, 恢复提示词.
---

# Stage Restore Prompt

Generates a self-contained restore prompt for the next hardware adaptation stage.

## When to Use

- User says "生成恢复提示" / "阶段切换" / "下一阶段提示" / "restore prompt"
- Before `/clear` to preserve context
- After completing a verification milestone

## Prerequisites

- Inside an openvela workspace (`.repo/` exists above working directory)
- `$CONTEST` team repository path known
- At least one build or verification cycle has been completed

## Steps

### 1. Collect Evidence

Run the evidence collection script:

```bash
bash <skill_dir>/scripts/collect-evidence.sh "$CONTEST"
```

This outputs: git branch/status, .config symbols, build artifact hashes/sizes, section layout.

### 2. Read Current State

From `$CONTEST/docs/`:
- Latest worklog (`*-worklog.md`) — what was done
- Latest verification (`verification/*.md`) — what was verified
- Latest review (`review/*.md`) — what was reviewed

### 3. Generate Restore Prompt

Assemble the fenced text block with these sections:

```
1. 路径约定（$WORKSPACE/$CONTEST/$SDK/$OUT/$FW）
2. 严格主模型/Agent 分工规则
3. 修改与操作边界
4. 当前阶段状态（已完成/进行中/未开始）
5. 已验证 vs 未验证清单
6. 关键文件清单
7. 构建产物 hash 表
8. 授权门禁
9. 下一阶段目标与验收标准
```

### 4. Output

Write to `$CONTEST/docs/next-stage-prompt-YYYY-MM-DD-<topic>.md`.

## Output Format

```markdown
# 下一阶段恢复提示词（YYYY-MM-DD）

把下面整段发给 Claude，用于 `/clear` 后恢复上下文。

```text
<restore prompt content>
```
```

## Rules

- No personal absolute paths — use `$WORKSPACE`, `$CONTEST`, `$SDK`, `$OUT`, `$FW`
- All SHA-256 hashes must be full 64-character strings
- Build artifacts must be verified against actual files (not from memory)
- "已验证" only for board-observed behavior; "未验证" for everything else
- Include the authorization gates from CLAUDE.md

## Reference

See `references/rv1126b-restore-examples.md` for real-world templates from the RV1126B adaptation.
