# logs/ — AI Coding 日志目录

存放你在开发中与 AI 工具的对话日志，和作品代码一并提交。

本目录已包含按官方格式导出的真实日志，后续继续按下述结构归档。

2026-11-03 补充 10 个 Claude Code 会话（3,212 条事件）及 37 个 Codex 会话
（46,819 条事件）。Codex 按原始会话的工作区筛选；另有 52 个文件只有元数据或
内部上下文，没有可导出的对话，未伪造会话正文。

公开副本保留原时间、事件顺序和工具调用关联，过滤内部推理、内部上下文及二进制
媒体。Codex 工具记录仅公开名称、调用 ID 和原始载荷 SHA-256；原始参数和返回值
留在本地。其他敏感内容以 `REDACTED` 标记并记录脱敏次数。正在进行的 Codex
会话截取至 2026-11-03 11:51 UTC，后续事件不在这次导出内。

## 内容边界

- `logs/lijian/` 是按大赛规范归档的 AI Coding 对话日志；其目录和文件命名遵循下述结构。
- 七个 `logs/bk7258-*` 顶层目录是早期 BK7258 串口抓取、安全启动和实板验收原始证据，
  不是 AI 对话日志。现有 `docs/verification/` 记录会引用这些历史证据，因此保留原路径；
  新的验收结论统一写入 `docs/verification/`，不再扩展这类顶层目录。

## 目录结构

```text
logs/
└── <github_login>/              # 你的 GitHub 用户名，一人一目录
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件（工具名与 session id 用 __ 连接）
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，由组委会提供的日志归集工具导出，**只提交 JSONL 本身**。

导出与提交的完整步骤、字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
