# 下一阶段恢复提示词（2026-07-15，提交 / PR / 日志后续）

把下面整段发给 Claude，用于 `/clear` 后恢复上下文。

```text
我们在 openvela 2026 大赛仓库继续，请严格遵守：

- 使用苏格拉底式交流：不确定时先停下来问我，不要自行扩展范围。
- 不要主动加载 skill；除非我明确要求。尤其构建/烧录验证时，先按文档给我步骤，由我在本机执行并回传结果。
- 只改 `$CONTEST` 队伍仓；不要直接改外层 `nuttx/`、`apps/`、`packages/`、`vendor/` 等官方 checkout。
- 需要理解代码时先用 CodeGraph，`projectPath` 用 `$WORKSPACE`。
- 不要使用 Workflow；本项目分析用普通 Agent 或直接只读检查。
- 不要在没有我确认的情况下执行耗时构建、SDK 打包、烧录、push、PR 创建、删除或覆盖操作。
- 文档中不要写个人电脑绝对路径；统一使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。

路径约定：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"
```

当前大赛提交规则要点：

- 官方提交指南说明：比赛期间 fork 专属仓进行开发，再以 PR 形式提交回专属仓，可自行 review 并合入。
- 若需改动 `nuttx` 等公共仓，不在本仓直接改；需 fork 对应公共仓并向 `dev-ai-contest-2026` 分支提 PR。
- AI Coding 日志需要提交到 `logs/`；工具只写本地文件，不会自动 push。
- README 目前先保持/恢复为官方模板风格，最终作品说明阶段再改成项目 README。

当前 git / 提交状态：

- 队伍仓：`$CONTEST`
- 当前本地分支：`submit-rv1126b-nsh-baseline`
- 该分支已推送到 fork，并跟踪：`fork/submit-rv1126b-nsh-baseline`
- 已推送功能 commit：`b41a694 enable ps in RV1126B NSH baseline`（如果本恢复提示页已被补提交，`git log -1` 可能显示后续文档 commit）
- push 目标 fork remote：
  `git@github.com-embracecactus:Embracecactus/contest2026_135_yongwangzhiqian.git`
- upstream / 专属仓 origin remote：
  `git@github.com-embracecactus:open-vela/contest2026_135_yongwangzhiqian.git`
- GitHub 提示的 PR 创建入口：
  `https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/pull/new/submit-rv1126b-nsh-baseline`

PR 应该这样创建：

- base repository: `open-vela/contest2026_135_yongwangzhiqian`
- base branch: `dev-ai-contest-2026`
- compare repository: `Embracecactus/contest2026_135_yongwangzhiqian`
- compare branch: `submit-rv1126b-nsh-baseline`

已提交 commit 摘要：

- `bea5ff0 document verified RV1126B NSH baseline`
  - `README.md`：恢复/保持官方大赛模板风格，暂不写最终作品 README。
  - `board/contest_board/configs/nsh/defconfig`：保留：
    - `CONFIG_RAW_BINARY=y`
    - `CONFIG_INTELHEX_BINARY=y`
  - 文档路径脱敏：Markdown 中不再保留个人电脑绝对路径。
  - 新增/更新文档：
    - `docs/next-stage-prompt-2026-07-15.md`
    - `docs/rv1126b-sdk-integration.md`
    - `docs/verification/2026-07-15-rv1126b-ec43ebb-amp-nsh-baseline.md`
    - `docs/verification/2026-07-15-rv1126b-1515-amp-nsh-recheck.md`
    - `docs/rv1126b-openvela-adaptation-research.md`
    - `docs/ai-worklog/...` 旧 prompt / worklog 路径脱敏
- `b41a694 enable ps in RV1126B NSH baseline`
  - `board/contest_board/configs/nsh/defconfig`：新增 `CONFIG_FS_PROCFS=y`。
  - `docs/rv1126b-sdk-integration.md`：更新 SDK 侧 `$OUT/rtt.bin` 符号链接安全替换流程，并记录 15:58 `ps` 复测证据。
  - `docs/verification/2026-07-15-rv1126b-1558-amp-ps-procfs.md`：新增 PROCFS / `ps` AMP 分区板测记录。

已验证事实：

1. 2026-07-15 14:00:58 基线记录：
   - `nuttx.bin` 大小 80320 bytes
   - 历史 hash：`0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075`
   - `$FW/amp.img` hash：`35f501c77ac77a27437160b8de190e9245ef736ee5b1d1d89634d28592618feb`
   - `$SDK/output/update/Image/update.img` hash：`89171c596e0df878ab0efa44d0f22007decc537853dae08281fee649fbe628a0`
   - AMP 分区板测通过，完整 `update.img` 未整包刷写。

2. 2026-07-15 15:15 复测记录：
   - 本轮 `nuttx.bin` hash：`96ed92c32fa13669589a1f067d1c9820305db3c3bcaec06d4c1744945c162cdf`
   - `nuttx.bin` / `$OUT/rtt.bin` 大小：80320 bytes
   - `mkimage` 输出：
     - `Data Size: 80320 Bytes`
     - `Architecture: ARM`
     - `Load Address: 0x48c02000`
     - `Hash value: 96ed92c32fa13669589a1f067d1c9820305db3c3bcaec06d4c1744945c162cdf`
   - 新 `$FW/amp.img` hash：`04b02fda18aef333d2ed66c25b5167c713487c2de739da292d991b39902d274b`
   - 板端 NSH 通过：
     - `help` 输出命令表
     - `uname -a` 输出：`NuttX 0.0.0 e02f581e23 Jul 15 2026 15:15:37 risc-v rv1126b_evb`
     - `ps: command not found` 是当前最小 defconfig 未启用 PROCFS，不作为失败项。
   - 本次未捕获：`update.img` 重新打包日志、`update.img` hash、实际烧录命令、完整 `update.img` 整包刷写。

3. 2026-07-15 15:58 PROCFS / `ps` 复测记录：
   - `board/contest_board/configs/nsh/defconfig` 已加入 `CONFIG_FS_PROCFS=y`。
   - `nuttx.bin` / `$OUT/rtt.bin` hash：`b428e9d2a259addc72574f2080c2038b9a948befa0fab29a48180e1e27619b43`
   - `nuttx.bin` / `$OUT/rtt.bin` 大小：98464 bytes
   - `$FW/amp.img` hash：`d390e0f738507ed58d59770e3a2dd9ee236f399f95134920dcc8336f69982835`
   - `$FW/amp.img` 大小：103424 bytes
   - `$OUT/rtt.bin` 在 SDK 中是符号链接；正确替换方式是保持链接不变，替换 `readlink -f "$OUT/rtt.bin"` 指向的真实目标文件。
   - 板端 NSH 通过：
     - `ps` 输出 `CPU0 IDLE` 与 `nsh_main`
     - `uname -a` 输出：`NuttX 0.0.0 e02f581e23 Jul 15 2026 15:58:19 risc-v rv1126b_evb`
   - 本次未捕获：`mkimage` 完整输出、实际 AMP 分区烧录命令、`update.img` 重新打包日志和 hash、完整 `update.img` 整包刷写。

AI logs 当前状态：

- `logs/` 下已有 `logs/lijian/...` 历史 JSONL 和 `manifest.json`。
- 提交 `bea5ff0` 前，`logs/` 没有未提交变化。
- 当时尝试：
  - `contest-snapshot --list` 失败：`contest-snapshot: command not found`
  - 预期路径下的 `validate-log.py` 未找到
- 因此本次没有强行同步日志。
- 下一阶段应该先只读检查：
  ```bash
  git -C "$CONTEST" status --short -- logs
  command -v contest-snapshot || true
  find "$WORKSPACE" -path '*/contest-log-collector/tools/validate-log.py' -o -name validate-log.py 2>/dev/null | head -20
  ```
- 如果本轮会话结束后日志自动写入 `logs/`，再询问我是否做单独提交，例如：
  ```bash
  git add logs/
  git commit -m "logs: sync AI sessions"
  git push
  ```

下一阶段建议顺序：

A. 先确认当前状态：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -1 --oneline
git -C "$CONTEST" remote -v
```

B. 如果我还没有创建 PR，指导我打开：

```text
https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/pull/new/submit-rv1126b-nsh-baseline
```

并确认 base / compare 是否正确。

C. 如 PR 已创建，检查是否需要补充 PR 描述。建议 PR 说明包含：

- 新硬件适配方向：RV1126B HPMCU openvela / NuttX NSH baseline。
- 已板测：AMP 分区更新后 NSH prompt、UART RX/TX、`help`、`uname -a`、`ps` 通过。
- 未声明：完整 `update.img` 整包刷写、RPMsg、CMake 等价、DCache 启用。
- AI logs 当前状态：历史日志已存在；本轮结束后如生成新日志，将单独同步。

D. 不要继续修改 README 为作品说明，除非我明确说进入“最终作品 README”阶段。

E. 如果我要补日志，先问清楚是否同步今天日志、是否 commit 到当前 PR 分支。

F. 如果我要继续验证，继续采用苏格拉底式确认范围：
   - 文档验证
   - 构建验证
   - SDK 打包验证
   - AMP 分区烧录验证
   - 完整 `update.img` 整包刷写验证

本恢复提示文件已按用户要求作为后续恢复文档补提交并推送到当前 PR 分支；后续只有内容继续变化时再单独提交。
```
