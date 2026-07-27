# BK7258 Git worktree 同步与 PR 交接记录（2026-07-27）

## 范围

本文只记录 Git worktree、分支、构建链接和提交流程，不包含代码内容分析。

## 同步前状态

| 项目 | 主检出目录 | clean worktree |
|---|---|---|
| 路径 | `contest2026_135_yongwangzhiqian/` | `.worktrees/bk7258-clean-pr/` |
| 分支 | `bk7258-n6-sdk-irq-bridge` | `bk7258-n6-sdk-irq-bridge-clean` |
| HEAD | `6d282a5` | `88e220f` |
| 跟踪分支 | `fork/bk7258-n6-sdk-irq-bridge` | `fork/bk7258-n6-sdk-irq-bridge-clean` |
| 工作区 | 8 个修改、3 个未跟踪文件 | clean |
| 相对各自 fork 分支 | 0 ahead / 0 behind | 0 ahead / 0 behind |

上游默认分支是 `origin/dev-ai-contest-2026`，不是 `main`。同步前：

- `bk7258-n6-sdk-irq-bridge` 相对当前上游落后 15 个提交，并保留旧开发历史；
- `bk7258-n6-sdk-irq-bridge-clean` 建立在当前上游之上，适合作为后续 PR 分支；
- 两条分支经过 rebase/cherry-pick 后提交 ID 已分化，不应直接 merge，也不应复制目录覆盖。

## 构建路径约束

manifest 创建的 app、QuickApp 和 board 链接均解析到主检出目录：

```text
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
```

它们不指向 `.worktrees/bk7258-clean-pr`。因此，从 openvela 工作区根目录构建时，实际使用的是主检出目录当前分支的内容。

## 已决定的同步策略

1. 在旧开发分支中把当前待提交修改整理成增量提交；`app/hello_app/.built` 作为本地生成标记单独保存，不进入提交。
2. 在增量提交完成后建立 `backup/bk7258-n6-sdk-irq-bridge-20260727`，保留完整同步前现场。
3. clean 分支只以 `origin/dev-ai-contest-2026` 为基线，不使用已经分叉的本地 `dev-ai-contest-2026`。
4. 只把新产生的增量提交 cherry-pick 到 `bk7258-n6-sdk-irq-bridge-clean`；禁止整分支 merge。
5. clean 分支确认完整并推送后，移除 secondary worktree，再让主检出目录切换到 clean 分支。
6. 最终从主检出目录执行构建和板测；用户负责创建 PR。

## 执行门禁

- 不读取或分析代码内容；如果 cherry-pick/rebase 产生需要理解代码的冲突，立即停止并保留现场。
- 不修改官方 `nuttx/`、`packages/`、`vendor/` 项目源码，只操作竞赛 overlay。
- 不删除未确认的本地产物；先保存或备份。
- 不使用普通 `--force`；只有发生已授权的历史改写时才考虑 `--force-with-lease`。

## 执行记录

- [x] 同步方案写入项目文档和持久记忆
- [ ] 保存旧分支当前增量提交
- [ ] 建立同步前 backup 分支
- [ ] 更新 clean 分支上游基线
- [ ] cherry-pick 增量提交
- [ ] 将 clean 分支迁回主检出目录
- [ ] 从工作区根目录验证构建
- [ ] 推送 PR 分支（PR 由用户创建）
