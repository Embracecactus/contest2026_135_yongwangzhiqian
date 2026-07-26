# Git Worktree 入门与本项目使用说明

本文面向不熟悉 Git 的开发者，说明本项目为什么使用 Git worktree、它和普通目录/分支有什么区别，以及 clean worktree 与 openvela 构建工作区之间的关系。

## 1. 一句话理解 worktree

一个 Git 仓库通常只能在一个目录里检出一个分支。Git worktree 允许同一个仓库同时拥有多个工作目录，每个目录检出不同分支。

可以把它理解成：

- Git 仓库是共用的“文件仓库”；
- 每个 worktree 是一张独立的工作桌；
- 不同工作桌可以摆放不同分支的文件；
- 所有工作桌共享已经保存到 Git 中的 commit。

```text
同一个 Git 仓库
├── 主工作区                         一张工作桌
│   └── 当前分支、未提交修改、暂存区
│
└── .worktrees/bk7258-clean-pr/     另一张工作桌
    └── BK7258 clean 分支、独立修改、独立暂存区
```

这里使用的是 `.worktrees`，不是 `.workflow`。worktree 是 Git 功能，与 Claude Workflow 或 GitHub Actions workflow 无关。

## 2. worktree 共享什么、隔离什么

| 内容 | 是否共享 | 说明 |
|---|---|---|
| Git commit | 共享 | 一个 worktree 创建的 commit，其他 worktree 也能看到 |
| tag 和 remote | 共享 | 指向同一个 Git 仓库 |
| Git 对象数据库 | 共享 | 不会重新复制一整套 Git 历史 |
| 当前分支 | 隔离 | 每个 worktree 可以检出不同分支 |
| 工作目录文件 | 隔离 | 未提交修改不会自动出现在另一个 worktree |
| 暂存区 | 隔离 | 一个 worktree 的 `git add` 不会暂存另一个 worktree 的文件 |
| untracked 文件 | 隔离 | 本地 SDK、构建产物等只存在于所在目录 |

因此，worktree 不是重新 clone 一份完整仓库，而是为同一个仓库增加一个独立工作目录。

## 3. 为什么不直接在主工作区修改

直接在主工作区修改是可行的。建立 clean worktree 不是 Git 的强制要求，而是本项目为了降低误提交风险做出的选择。

当前主工作区同时承担 openvela 构建环境职责，可能包含：

- 本地安装、checksum-pinned 的 BK SDK bundle；
- manifest/linkfile 创建的映射和软链接；
- 构建生成文件；
- 本地验证文件；
- 其他阶段留下但不能随意删除的 untracked 内容；
- 曾经处理过的其他硬件目标或分支上下文。

这使主工作区更像“施工现场”。代码可以在这里修改和构建，但整理 PR 时容易受到无关文件和历史的干扰。

clean worktree 则是一张专门整理 BK7258 交付代码的干净工作桌：

```text
主工作区：构建现场
  ├── SDK bundle
  ├── manifest/linkfile
  ├── 构建产物
  └── 本地未跟踪内容

clean worktree：提交现场
  ├── 干净 BK7258 分支
  ├── 当前阶段源码
  ├── 当前阶段文档
  └── 准备提交的最小 diff
```

## 4. 四个主要优点是怎样产生的

### 4.1 保持干净的 BK7258 分支和提交历史

严格来说，干净历史来自“从正确基线创建的干净分支”。worktree 的作用是让这个分支可以一直保留在单独目录中，而不需要反复切换主工作区。

例如：

```text
官方目标分支
└── BK7258 clean 分支
    ├── BK7258 基础移植 commit
    ├── IRQ bridge commit
    └── GPIO commit
```

只要没有把其他硬件目标的 commit 合入该分支，最终 PR 就只包含 BK7258 相关历史。

需要注意：worktree 本身不会自动删除错误历史。真正决定 PR 历史的是 worktree 中检出的分支和它的 commit 祖先。

### 4.2 隔离 SDK、构建产物、软链接和未提交内容

主工作区中的 untracked 文件不会自动出现在 clean worktree。

因此，在 clean worktree 中运行 `git status` 时，通常只会看到当前阶段真正修改的源码和文档，而不会被主工作区的 SDK bundle、构建痕迹等内容干扰。

这能降低以下风险：

- `git add -A` 时误加入无关文件；
- 将生成文件误认为产品源码；
- 清理构建环境时误删需要保留的本地文件；
- 审查 diff 时被大量无关改动淹没。

### 4.3 避免其他目标历史进入 BK7258 PR

如果直接在一个包含其他目标历史的分支上继续提交，GitHub 会根据分支祖先关系计算 PR 内容，可能把以前尚未进入目标分支的 commit 一并列入 PR。

clean worktree 检出的是从正确官方基线建立的 BK7258 clean 分支。只把确认需要的 BK7258 commit 放入该分支，就能保持 PR 边界清晰。

这里仍然要区分两个概念：

- worktree：隔离工作目录；
- branch：决定提交历史。

两者配合才能得到“干净目录 + 干净历史”。

### 4.4 方便回退、对比和制作 commit

clean worktree 的 `git diff` 基本只显示当前 BK7258 阶段的改动，因此更容易回答：

- 本阶段究竟改了哪些文件；
- 是否修改了官方 NuttX tree；
- 是否混入了其他硬件目标；
- 当前 commit 是否可以独立回退；
- 文档是否与代码状态同步。

在 clean worktree 中回退未提交文件，也不会直接改变主构建工作区中的本地 SDK、软链接和生成内容。

## 5. 当前 BK SDK 与 worktree 的关系

worktree 不参与 BK SDK 的运行时映射。当前 BK SDK 适配链是：

```text
NuttX application / upper-half
        ↓
团队 overlay lower-half
        ↓
BK SDK API headers
        ↓
固定版本的预编译 BK SDK archives
        ↓
BK7258 hardware
```

IRQ bridge 是特殊路径：

```text
BK SDK source 0..63
        ↓  source + 16
NuttX logical IRQ 16..79
        ↓
NuttX IRQ / NVIC / RAM vector
        ↓
BK SDK callback
```

例如：

- TIMER source 3 → NuttX IRQ 19；
- GPIO_S source 55 → NuttX IRQ 71；
- GPIO_NS source 37 → NuttX IRQ 53。

`.worktrees/bk7258-clean-pr` 只负责保存和审查团队 overlay 的权威源码，不改变上述映射关系。

## 6. 为什么 clean worktree 不能直接代表当前构建输入

完整 openvela workspace 由 manifest/linkfile 组织。当前生成工作区中的链接仍指向主 contest checkout，而不是 clean worktree。

可以使用下面的变量理解目录关系：

```text
$WORKSPACE                 完整 openvela workspace
$CONTEST                   manifest 当前指向的主 contest checkout
$CLEAN                     .worktrees/bk7258-clean-pr
```

因此：

- 在 `$CLEAN` 修改源码，主 workspace 不会自动使用这些修改；
- 从 `$WORKSPACE` 构建时，实际读取的仍是 `$CONTEST`；
- 获得构建授权后，需要把当前阶段改动临时、精确地同步到 `$CONTEST`；
- 构建和验证结束后，再恢复 `$CONTEST` 的临时状态；
- 最终 commit 始终在 `$CLEAN` 中整理。

这一步增加了少量操作，但可以同时保留：

1. 可工作的完整 openvela 构建现场；
2. 不受构建环境污染的干净提交现场。

## 7. 本项目的推荐工作流程

```text
1. 在 clean worktree 编写当前阶段代码
2. 做范围受限的源码审查
3. 驱动适配使用 nuttx-driver-development 查缺补漏
4. 立即更新当前 Stage worklog
5. 等待用户明确授权构建
6. 将精确改动临时同步到主 contest checkout
7. 从完整 openvela workspace 执行 clean build
8. 记录配置、ELF、产物长度和哈希
9. 恢复主 contest checkout 的临时改动
10. 在 clean worktree 制作可回退 commit
11. 只有明确授权后才 push 或创建 PR
```

## 8. Git 初学者常见误解

### “worktree 是完整复制仓库吗？”

不是。它共享原仓库的 commit、tag、remote 和 Git 对象，只增加一个工作目录及其独立状态。

### “删除 worktree 会删除 commit 吗？”

正常移除 worktree 不会删除已经创建的 commit，但未提交修改可能丢失，所以不能直接粗暴删除含修改的目录。

### “有 worktree 就一定有干净历史吗？”

不一定。历史是否干净取决于检出的分支。worktree 只提供隔离目录。

### “一个分支能同时在两个 worktree 中检出吗？”

Git 通常会阻止同一分支同时被两个 worktree 检出，避免两个目录同时修改同一个分支造成混乱。

### “主工作区完全干净时还需要 worktree 吗？”

不一定。如果只有一个目标、一个分支、没有本地 SDK/生成内容，也不需要并行保留其他环境，直接在主工作区开发更简单。

## 9. 当前项目结论

本项目使用 clean worktree，并不是因为主工作区不能修改，而是因为主工作区承担完整 openvela 构建现场职责。

最简单的概括是：

> 主工作区是施工现场，clean worktree 是整理最终交付代码的干净桌面。

它的价值来自“独立工作目录”和“干净 BK7258 分支”共同作用，而不是 worktree 自动解决所有 Git 历史问题。
