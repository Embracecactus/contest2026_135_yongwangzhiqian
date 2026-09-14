---
name: publish-pr-cn
description: 傻妞仓库的中文 PR 发布适配。用于本项目提交、推送 fork 或准备中文 PR 内容，将项目远端、基线与文案要求交给通用 fork-change-publication 流程执行。不用于其他项目，不自动创建或合并 PR。
---

# 发布中文 PR

此处只保存项目适配，不再维护另一份通用 Git 发布正文。
执行时先从宿主 Skill 目录加载 `fork-change-publication/SKILL.md`。
当前用户级安装位置为 `~/.agents/skills/fork-change-publication/`。
若该通用 Skill 不可用，保留当前工作树并报告缺失；不要假装已完成发布检查。

## 权限与项目约束

- 确认仓库为 `contest2026_135_yongwangzhiqian`。官方仓库是
  `open-vela/contest2026_135_yongwangzhiqian`；开发 fork 是
  `Embracecactus/contest2026_135_yongwangzhiqian`。
- 从当前 remote URL 解析官方基线，不再硬编码 `origin`。默认基线分支
  `dev-ai-contest-2026`；用户指定值优先。本轮官方 remote 为 `openvela`，
  这只是当前映射，下一次仍核对 URL。
- 推送目标仍是核验 URL 后的个人 `fork`；不向官方仓库推送。
  提交、推送、创建 PR、合并分别沿当前明确授权执行，不把烧录授权当发布授权。
- 保留无关脏树。默认不改写历史、不强推、不自动 rebase/merge、不清理文件。
  用户通常自行在网页创建/合并 PR；未要求时仅交付可粘贴内容。
- 默认排除硬件采集、`out/`、构建输出、签名产物、私钥和设备备份。
  比赛开发日志仅在独立获准的日志收口范围内处理，不顺带暂存。

## 中文 PR 输出

按通用流程完成请求范围内的检查/发布。基于已核验的官方基线与实际发布 HEAD
生成内容；未提交文件不能写成已在该 PR 中。输出：

- 中文 PR 标题：保留 Conventional Commit 类型/范围，摘要使用中文。
- 中文正文包含具体问题、实际修改、相关验证、边界与未完成项；简单变化可用
  连贯短段落，不机械展开长模板。只写有证据的构建、版本和板端结果。
- 若有明确延后、硬件夹具依赖、破坏性测试或未覆盖项，单独列明，不能隐去。
- 上游仓库的 Compare/创建 PR 链接，基线默认 `dev-ai-contest-2026`，head 为 `Embracecactus:<当前分支>`。
- 本地提交 SHA、远程分支 SHA 和推送状态。

最终明确说明 PR 尚未代用户创建，用户可复制中文标题/正文并通过链接完成远程操作。
