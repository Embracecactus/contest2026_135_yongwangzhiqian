# 下一阶段恢复提示词（2026-07-15，RV1126B 适配继续）

把下面整段发给 Claude，用于 `/clear` 后恢复上下文，继续 RV1126B HPMCU openvela / NuttX 适配与提交收尾。

```text
我们在 openvela 2026 大赛仓库继续，目标是继续 RV1126B HPMCU openvela / NuttX 适配与提交收尾。请严格遵守：

- 使用苏格拉底式交流：不确定时先停下来问我，不要自行扩展范围。
- 不要主动加载 skill；除非我明确要求。尤其构建 / 烧录 / SDK 打包验证时，先按文档给我步骤，由我在本机执行并回传结果。
- 只改 `$CONTEST` 队伍仓；不要直接改外层 `nuttx/`、`apps/`、`packages/`、`vendor/` 等官方 checkout。
- 需要理解代码时先用 CodeGraph，`projectPath` 用 `$WORKSPACE`。
- 不要使用 Workflow；本项目分析用普通 Agent 或直接只读检查。
- 不要在没有我确认的情况下执行耗时构建、SDK 打包、烧录、push、PR 创建、删除或覆盖操作。
- 文档中不要写个人电脑绝对路径；统一使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。
- README 目前先保持官方模板风格，最终作品说明阶段再改成项目 README。

路径约定：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"
```

当前大赛提交规则要点：

- 比赛期间 fork 专属仓进行开发，再以 PR 形式提交回专属仓，可自行 review 并合入。
- 若需改动 `nuttx` 等公共仓，不在本仓直接改；需 fork 对应公共仓并向 `dev-ai-contest-2026` 分支提 PR。
- AI Coding 日志需要提交到 `logs/`；工具只写本地文件，不会自动 push。
- 当前主要开发边界仍是 `$CONTEST/board/contest_board/`、`docs/`、`logs/`。

当前 git / 提交状态：

- 队伍仓：`$CONTEST`
- 当前本地分支：`submit-rv1126b-nsh-baseline`
- 跟踪远端：`fork/submit-rv1126b-nsh-baseline`
- push 目标 fork remote：
  `git@github.com-embracecactus:Embracecactus/contest2026_135_yongwangzhiqian.git`
- upstream / 专属仓 origin remote：
  `git@github.com-embracecactus:open-vela/contest2026_135_yongwangzhiqian.git`
- GitHub PR 创建入口：
  `https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/pull/new/submit-rv1126b-nsh-baseline`
- PR 应该这样创建：
  - base repository: `open-vela/contest2026_135_yongwangzhiqian`
  - base branch: `dev-ai-contest-2026`
  - compare repository: `Embracecactus/contest2026_135_yongwangzhiqian`
  - compare branch: `submit-rv1126b-nsh-baseline`

最近已推送 commits：

```text
a002df8 logs: backfill AI sessions
cf7ac6d docs: add submission follow-up restore prompt
b41a694 enable ps in RV1126B NSH baseline
bea5ff0 document verified RV1126B NSH baseline
ec43ebb RV1126B HPMCU NuttX port: cleanup, documentation, and AI logs
```

当前最新一次只读状态检查显示：分支与 fork 远端同步，但本轮会话日志又被采集器追加了未提交变化；随后又新增了本恢复提示文档，所以新会话开始时应先检查真实状态：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" status --short -- logs docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
```

上一次看到的 logs 未提交变化为：

```text
 M logs/lijian/2026-07-15/claude-code__1fec91da-52fb-46a4-ae4f-025cf59d2bb8.jsonl
 M logs/lijian/manifest.json
```

本恢复提示文档本身路径：

```text
docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
```

下一阶段开始时，先检查 logs 和提示文档是否需要单独提交：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" status --short -- logs docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

如果校验通过，再问我是否单独提交 / 推送最新会话日志和本恢复提示文档。建议命令为：

```bash
git -C "$CONTEST" add logs/ docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
git -C "$CONTEST" commit -s -m "logs: sync latest session"
git -C "$CONTEST" push
```

注意：不要擅自提交 / push，先问我。

已验证事实：

1. 2026-07-15 14:00:58 基线记录：
   - `nuttx.bin` 大小：80320 bytes
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
     - 当时 `ps: command not found` 是最小 defconfig 未启用 PROCFS，不作为失败项。
   - 本次未捕获：`update.img` 重新打包日志、`update.img` hash、实际烧录命令、完整 `update.img` 整包刷写。

3. 2026-07-15 15:58 PROCFS / `ps` 复测记录：
   - `board/contest_board/configs/nsh/defconfig` 已加入：
     ```text
     CONFIG_FS_PROCFS=y
     ```
   - `nuttx.bin` / `$OUT/rtt.bin` hash：`b428e9d2a259addc72574f2080c2038b9a948befa0fab29a48180e1e27619b43`
   - `nuttx.bin` / `$OUT/rtt.bin` 大小：98464 bytes
   - `$FW/amp.img` hash：`d390e0f738507ed58d59770e3a2dd9ee236f399f95134920dcc8336f69982835`
   - `$FW/amp.img` 大小：103424 bytes
   - `$OUT/rtt.bin` 在 SDK 中是符号链接；正确替换方式是保持链接不变，替换：
     ```bash
     RTT_TARGET="$(readlink -f "$OUT/rtt.bin")"
     cp -av "$WORKSPACE/nuttx/nuttx.bin" "$RTT_TARGET"
     ```
   - 板端 NSH 通过：
     ```text
     NuttShell (NSH)
     nsh> ps
       PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK            STACK COMMAND
         0     0   0 FIFO     Kthread   - Ready              0000000000000000 0002016 CPU0 IDLE
         6     6 100 FIFO     Task      - Running            0000000000000000 0004032 nsh_main
     nsh> uname -a
     NuttX 0.0.0 e02f581e23 Jul 15 2026 15:58:19 risc-v rv1126b_evb
     ```
   - 本次未捕获：`mkimage` 完整输出、实际 AMP 分区烧录命令、`update.img` 重新打包日志和 hash、完整 `update.img` 整包刷写。

已提交 / 更新的重要文件：

- `board/contest_board/configs/nsh/defconfig`
  - 包含 `CONFIG_RAW_BINARY=y`
  - 包含 `CONFIG_INTELHEX_BINARY=y`
  - 已新增 `CONFIG_FS_PROCFS=y`
- `docs/rv1126b-sdk-integration.md`
  - 记录 SDK AMP 打包主流程。
  - 已更新 `$OUT/rtt.bin` 符号链接安全替换流程。
  - 已记录 15:58 `ps` 通过的 artifact / 串口证据。
- `docs/verification/2026-07-15-rv1126b-ec43ebb-amp-nsh-baseline.md`
- `docs/verification/2026-07-15-rv1126b-1515-amp-nsh-recheck.md`
- `docs/verification/2026-07-15-rv1126b-1558-amp-ps-procfs.md`
- `docs/next-stage-prompt-2026-07-15-submission-follow-up.md`
- `docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md`
- `docs/rv1126b-openvela-adaptation-research.md`
- `docs/ai-worklog/...`

AI logs 当前状态：

- `.claude` 工具仓已通过以下方式同步成功：
  ```bash
  repo sync -c --no-manifest-update .claude
  ```
- `contest-log-collector` 已安装并健康检查通过：
  ```text
  Passed: 11
  Failed: 0
  ✅ All checks passed.
  ```
- `contest-snapshot` 已可用：
  ```text
  /home/lijian/.local/bin/contest-snapshot
  ```
- 已执行：
  ```bash
  contest-snapshot --backfill --github-login lijian
  ```
  结果：发现 93 个 transcript，导出到 `logs/lijian/2026-07-15/`。
- `validate-log.py` 初次对旧 8 个日志报 orphan warning；已按用户确认选择“清理后提交”，删除旧 orphan JSONL，并重新校验：
  ```text
  Files checked:  93
  Events checked: 25922
  ✅ ALL OK
  ```
- 已提交并推送：
  ```text
  a002df8 logs: backfill AI sessions
  ```
- 推送后，采集器又对当前会话追加日志，导致当前有 2 个 logs 文件未提交变化；下一阶段先校验后询问是否提交。

外层 workspace 注意事项：

- `$WORKSPACE/.repo/manifests/openvela.xml` 有本地改动，把 remote 从相对路径切到 Gitee：
  ```diff
  -  <remote fetch="../open-vela/" name="openvela"/>
  -  <remote fetch="../" name="git"/>
  +  <remote fetch="https://gitee.com/open-vela/" name="openvela"/>
  +  <remote fetch="https://gitee.com/" name="git"/>
  ```
- 这不是 `$CONTEST` 队伍仓改动，不要擅自 restore / reset。
- 如需同步 manifest 或全量 `repo sync`，必须先问我；目前只用过：
  ```bash
  repo sync -c --no-manifest-update .claude
  ```

下一阶段建议顺序：

A. 先做只读状态确认：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" remote -v
git -C "$CONTEST" status --short -- logs docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

B. 如果 logs / 本恢复提示文档仍有增量且校验通过，问我是否单独提交 / 推送：

```bash
git -C "$CONTEST" add logs/ docs/next-stage-prompt-2026-07-15-rv1126b-adaptation-continuation.md
git -C "$CONTEST" commit -s -m "logs: sync latest session"
git -C "$CONTEST" push
```

C. 检查 PR 是否已创建；如果没有，指导我打开：

```text
https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/pull/new/submit-rv1126b-nsh-baseline
```

并确认：

- base repository: `open-vela/contest2026_135_yongwangzhiqian`
- base branch: `dev-ai-contest-2026`
- compare repository: `Embracecactus/contest2026_135_yongwangzhiqian`
- compare branch: `submit-rv1126b-nsh-baseline`

D. PR 描述建议包含：

- 新硬件适配方向：RV1126B HPMCU openvela / NuttX NSH baseline。
- 已板测：
  - AMP 分区更新后 NSH prompt 通过。
  - UART RX/TX 通过。
  - `help` 通过。
  - `uname -a` 通过。
  - `ps` 通过，输出 `CPU0 IDLE` 与 `nsh_main`。
- 已记录 artifact：
  - 14:00 基线 `nuttx.bin` / `$FW/amp.img` / `update.img` hash。
  - 15:15 AMP 复测 hash 与 `mkimage` 关键输出。
  - 15:58 PROCFS / `ps` 复测 hash 与串口输出。
- 未声明：
  - 完整 `update.img` 整包刷写通过。
  - RPMsg / Linux A-core 与 HPMCU 通信完成。
  - CMake 构建与 classic Make 等价。
  - DCache 启用或完成验证。
- AI logs：
  - 已安装 collector。
  - 已 backfill 93 个 Claude Code sessions。
  - 已通过 `validate-log.py`。
  - 若当前会话生成新的日志增量，将单独同步。

E. 如果继续适配功能，继续采用苏格拉底式确认范围。可选方向：

1. 文档提交 / PR 收尾。
2. 完整 `update.img` 整包刷写验证。
3. SDK patch 清单精确化：
   - DTS / DTSI
   - `rv1126b-amp.dtsi`
   - `amp_mcu.its`
   - package-file 中 `amp    amp.img` 的精确路径。
4. RPMsg / Linux A-core 与 HPMCU 通信探索。
5. DCache / 时钟 / 中断 / 定时器进一步完善。
6. CMake 构建等价性验证。
7. 最终作品 README 阶段。

不要继续修改 README 为作品说明，除非我明确说进入“最终作品 README”阶段。
```
