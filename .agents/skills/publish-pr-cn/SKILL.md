---
name: publish-pr-cn
description: 在 contest2026_135_yongwangzhiqian 项目中安全完成任务范围内的提交、推送到个人 fork，并生成可直接粘贴的中文 PR；适用于“提交推送并写中文 PR”等发布请求。
---

# 发布中文 PR

在本仓库内完成“检查范围 → 提交 → 推送 fork → 生成中文 PR”工作流。保留用户的无关改动和未跟踪文件，不替用户在远程创建 PR。

## 权限与项目约束

- 只有当前请求明确授权提交时才执行 `git commit`，明确授权推送时才执行 `git push`；缺少授权时只检查并准备结果。
- 只操作当前工作树，确认仓库根目录是 `contest2026_135_yongwangzhiqian`。
- 默认目标分支为 `origin/dev-ai-contest-2026`；用户明确指定其他基线时使用其指定值。
- 推送目标只能是名为 `fork` 的个人仓库，预期所有者为 `Embracecactus`；禁止向 `origin` 推送。
- 不执行强制推送，不改写已有提交，不自动 rebase/merge，不删除或清理文件。遇到非 fast-forward、冲突或远程不符时停止并说明。
- 用户通常在网页端创建 PR。除非用户另外明确要求，否则不运行 `gh pr create` 或其他创建 PR 的 API。

## 发布流程

1. 运行只读预检：确认仓库根、当前非 detached/非基线分支、`git status --short --branch`、`git remote -v`，以及相对目标分支的提交范围。
2. 检查当前任务实际修改的文件。仅用显式路径暂存本任务文件；禁止使用 `git add -A`、`git add .` 或会顺带暂存无关修改的命令。
3. 默认排除硬件调试采集目录、日志、构建输出、缓存、下载包、签名产物、凭据、私钥和临时生成的密钥。尤其不要纳入：
   - `hardware-debug-*`
   - `logs/hardware-debug/`
   - `tools/windows-hardware-debug/scripts/*.log`
   - `tools/windows-hardware-debug/scripts/*.jlink`
   - `out/`、构建目录及任何私钥/凭据文件
4. 提交前审查 `git diff --cached --stat`、`git diff --cached --name-status` 和实际暂存差异；如果范围不清楚，不猜测暂存。
5. 执行与风险相称的验证，并至少运行 `git diff --cached --check`。可复用本轮已经取得且仍对应当前代码的测试结果，避免无意义地重复耗时测试。失败项必须如实保留，不能写成通过。
6. 若存在尚未提交的任务改动，使用 Conventional Commit 风格的中文提交信息，例如 `feat(bk7258): 补齐 P0 xTS 回归`。已有正确提交时保留它们，不 amend、不制造空提交。
7. 再次确认当前分支与远程，只将解析后的当前分支推送到 `fork` 并设置上游，等价于 `git push --set-upstream fork <当前分支>`。不要把未跟踪文件误认为已推送内容。
8. 推送后读取远程分支 SHA，确认与本地 `HEAD` 一致。

## 中文 PR 输出

基于 `origin/<基线>...HEAD` 的实际提交生成内容，不根据未提交文件编写。输出：

- 中文 PR 标题：保留 Conventional Commit 类型/范围，摘要使用中文。
- 中文 PR 正文，至少包含“背景”“本次修改”“验证”“边界与未完成项”；只写有证据的测试、构建、固件版本、哈希和板端结果。
- 若有明确延后、硬件夹具依赖、破坏性测试或未覆盖项，单独列明，不能隐去。
- 上游仓库的 Compare/创建 PR 链接，基线默认 `dev-ai-contest-2026`，head 为 `Embracecactus:<当前分支>`。
- 本地提交 SHA、远程分支 SHA 和推送状态。

最终明确说明 PR 尚未代用户创建，用户可复制中文标题/正文并通过链接完成远程操作。
