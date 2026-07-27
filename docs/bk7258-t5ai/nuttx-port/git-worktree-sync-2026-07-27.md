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
- [x] 保存旧分支当前增量提交：`0a7574f`（同步文档）、`6b2d98c`（SDK IRQ/library integration）
- [x] 建立同步前 backup 分支：`backup/bk7258-n6-sdk-irq-bridge-20260727` → `6b2d98c`
- [x] 将 `app/hello_app/.built` 单独保存到 stash，未删除、未提交
- [x] 获取最新上游并将 clean 分支 rebase 到 `origin/dev-ai-contest-2026`
- [x] 同步文档提交到 clean 分支：`0f4a025`
- [x] 识别功能增量的 AP 前置依赖：旧提交 `38699e8`
- [x] 迁移 AP 前置提交并保留 clean 分支已有 GPIO/N6 状态：`a0271af`
- [x] 迁移功能增量 `6b2d98c`：clean 提交 `b07f949`
- [x] 移除 secondary worktree，将 `bk7258-n6-sdk-irq-bridge-clean` 迁回主检出目录
- [x] 从主检出目录完成 CP → AP → CP restore 构建，exit 0
- [x] 使用精确 `--force-with-lease` 更新 fork PR 分支；PR 由用户创建

### 冲突处理结果

第一次直接迁移 `6b2d98c` 时，AP 相关 Make/defconfig 产生冲突。检查提交依赖后确认 clean 分支缺少 `38699e8` 的 CPU1 AP bring-up 前置实现。随后按依赖顺序处理：

1. 撤销失败的功能 cherry-pick；
2. 迁移 `38699e8`，合并 AP 控制与 clean 分支已有 GPIO app/build 配置，并保留更新后的 N6 历史证据；
3. 再次迁移 `6b2d98c`，自动合并成功；
4. `git diff --check` 与 `git fsck --no-dangling` 均通过，两个 worktree 均为 clean。

### 主检出目录与构建结果

secondary worktree 已在其内容全部提交后移除。当前唯一 worktree 为：

```text
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
branch: bk7258-n6-sdk-irq-bridge-clean
```

manifest linkfile 因路径不变而自动使用 clean 分支内容。完整构建命令
`board/bk7258_t5ai/scripts/build_dual_image.sh` exit 0，日志为
`/tmp/bk7258-dual-build-sync-2026-07-27.log`。构建完成 bootloader → CP → AP → CP restore，
并通过 root/manifest consistency gate。CP map 只使用 CP SDK 路径，AP map 只使用 AP SDK
路径。最终 root CP-only `all-app.bin` 为 252348 B；normal update segments 为：

```text
bl_crc.bin@0x0-0x11000
app_crc.bin@0x11000-0x2c9bc
app1_crc.bin@0x220000-0x10b16
```

未执行烧录或板测，状态仅为 `build-verified`。

### 推送结果

远端 clean 分支原 tip 为 `88e220f`。由于本地已 rebase，使用绑定该旧 tip 的精确
`--force-with-lease` 推送成功，构建与文档提交 `bb33268` 已进入
`fork/bk7258-n6-sdk-irq-bridge-clean`。没有创建 PR，PR 由用户完成。
