# 文档分层与导航

[English](README_EN.md) | 简体中文

文档按结论的适用范围分层。单板观察不能升级为 SoC 契约，历史验收也不能覆盖
当前源码、配置和构建产物。

| 层级 | 目录 | 内容与边界 |
|---|---|---|
| Contest delivery | [`contest/`](contest/) | 当前发布的技术报告快照与照片；[决赛提交与初赛后更新](contest/决赛提交与初赛后更新.md)；[答辩与影片源码](contest/答辩材料/README.md)随仓库发布；PPT/PDF 与成片以 Release 附件交付，官方模板、原始实拍与分轨仍留本地；表单状态以交付清单为准 |
| Chip / SoC | [`chips/<soc>/`](chips/) | 不依赖具体板卡的寄存器、启动、IRQ、时钟、PM、SDK ABI 和通用调试契约 |
| Platform integration | [`platforms/<soc>/`](platforms/) | 跨板构建、启动与更新模型、交付方法、符合性说明，以及明确标为历史的工程记录 |
| Verification | [`verification/<soc>/`](verification/) | 带日期、构建身份和适用边界的不可变主机/实板验收记录 |

板型、profile、分区和发布策略是源码/配置事实，不在 `docs/` 复制维护。BK7258 的
权威入口是 `boards/bk7258/CONFIGS.md` 与 `boards/bk7258/README.md`。

## BK7258 入口

| 要做什么 | 入口 |
| --- | --- |
| 了解作品、看视频、获取答辩文件或构建 | [项目 README](../README.md) |
| 使用手机连接、配置和更新傻妞 | [Android 操作说明](../android/shaniu-companion/README.md) |
| 查询当前支持与验收边界 | [CONFIGS.md](../boards/bk7258/CONFIGS.md) |
| 查构建、部署、硬件与产品合同 | [平台入口](platforms/bk7258/README.md) / [芯片契约](chips/bk7258/README.md) |
| 跟做傻妞认领、语音、资源、电脑工作台、OTA 与评委复现 | [上手与评委复核](platforms/bk7258/shaniu-user-guide.md) / [功能与证据清单](platforms/bk7258/shaniu-feature-inventory.md) |
| 决赛提交清单、答辩安排与初赛后更新 | [决赛提交与初赛后更新](contest/决赛提交与初赛后更新.md) |
| 查看本轮实测、录像和缺陷处理 | [11-04 修复、778/App 59 下载与验收边界](verification/bk7258/2026-11-04-finals-review-repairs.md)；[11-03 功能复测](verification/bk7258/2026-11-03-feature-validation/README.md)保留为历史录像证据 |
| 查来源和许可证 | [SOURCE_PROVENANCE.md](../SOURCE_PROVENANCE.md) |

历史资料：[日期化验收记录](verification/bk7258/) ·
[Master Plan 历史决策](platforms/bk7258/shaniu-master-plan.md) ·
[9 月 20 日技术报告](contest/技术报告-BK7258三核适配与傻妞AI伴侣.md) ·
[9 月 20 日初赛评审流程快照](contest/初赛评审流程与实测快照-20260920.md)。

阅读历史资料时，以其记录日期和构建身份为边界。凡标题或开头标为“历史”“快照”
或“retired”的内容，只用于解释当时决策，不能作为当前命令、信任根、分区或板型状态。

## 文档维护

仅在编辑或清理相关文档时应用以下规则，路径均相对仓库根目录。

- `docs/verification/bk7258/` 保存带日期的不可变验收证据，不是实时任务表。
  获准清理时保留独有的硬件和逆向证据，只处理授权范围内的上游手册副本、
  已完成提示词、工作流水、过时计划、重复评审及仅解释已退役中间布局的文档。
- 同一文档树内遵循官方 Markdown 链接约定。跨树源码或配置引用若需要三层以上
  父目录跳转，优先写仓库根相对的逻辑路径，如 `boards/bk7258/CONFIGS.md`，
  不写本机绝对路径或绑定仓库目录名的 URL。不为保留过时链接创建占位文件。
- 移动或删除文档后，检查并修复本次变更影响的入链和相关状态声明，清理本次
  操作产生的空目录。既有无关断链或旧状态不扩大为全库清理；报告实际删除内容
  和刻意保留的证据类别。
