# 03｜权威来源地图

学习硬件适配时，最危险的习惯是“找到一段看起来合理的文字或代码，就把它当成最终事实”。同一个问题往往同时涉及仓库所有权、固定版本源码、有效配置、链接产物和板端行为；它们分别回答不同问题，不能互相代替。

> **来源记录**
>
> - 教学主题：BK7258 学习过程中的来源选择、证据等级与冲突处理
> - source ref：`$CONTEST` 的 `HEAD`（撰写时）
> - source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 最后核对日期：2026-07-24
> - 直接来源：[`$IMPL/README.md`](../../../bk7258-t5ai/README.md)、[`$IMPL/next-stage-prompt.md`](../../../bk7258-t5ai/next-stage-prompt.md)、[`$BOARD`](../../../../board/bk7258/)、[contest manifest](../../../../contest2026_135_yongwangzhiqian.xml)
> - 教学简化：本文规定“应该去哪里取证”，不复述 current Stage、当前阻塞或下一实施动作

## 1. 路径变量

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export IMPL="$CONTEST/docs/bk7258-t5ai"
export LEARN="$CONTEST/docs/learning/bk7258-t5ai"
export BOARD="$CONTEST/board/bk7258"
```

这里的 `$IMPL` 指实施文档入口；正在开发的源码可能位于另一个 Git worktree。引用源码时必须另外记录实际仓库、分支和 commit，不能只写“当前目录”或“最新版”。

## 2. 先问：你究竟想证明什么

| 问题 | 第一权威来源 | 还需要什么 | 不能单独作为结论的材料 |
|---|---|---|---|
| 哪个仓库拥有这份代码？ | [contest manifest](../../../../contest2026_135_yongwangzhiqian.xml)和各 Git 仓根 | linkfile/项目映射的只读核对 | 工作区里恰好出现的同名文件 |
| 当前实施做到哪里？ | [`$IMPL/next-stage-prompt.md`](../../../bk7258-t5ai/next-stage-prompt.md)及其指向的 worklog | 对应分支、checkpoint 和最新证据 | 教学文章、旧恢复提示、聊天印象 |
| 某个函数当前怎样实现？ | 实际实施仓中固定 commit 的源码 | 调用者、配置、构建后端和动态注册关系 | 函数名、注释、Graphify 单独输出 |
| 某个选项是否启用？ | 本次构建生成的 `.config` | Kconfig 定义与 defconfig 请求值 | 只看 defconfig 或 `#ifdef` |
| 某个源文件是否进入固件？ | 编译记录、对象、ELF/map 和最终打包链 | 有效配置及当前构建后端 | 文件存在、CMake/Make 中出现路径 |
| 某条路径是否在板上执行？ | 与该镜像匹配的串口、SWD、探针或功能观察 | 镜像哈希、命令、前置条件和重复结果 | 静态源码、成功编译、旧板测日志 |
| NuttX 接口应怎样使用？ | 固定 commit 的上游 NuttX 源码及官方文档 | 当前配置和该板适配边界 | 只凭其他 RTOS 或 Linux 经验类推 |
| 寄存器字段是什么意思？ | 对应芯片版本的硬件手册/SDK 定义 | 实际芯片、修订版及访问上下文 | 魔数、反编译猜测、无版本网页摘要 |

## 3. 七层证据阶梯

```text
E0 目录或文字中出现
  ↓
E1 固定 commit 的源码存在
  ↓
E2 本次有效配置选择了它
  ↓
E3 构建成功并生成对象
  ↓
E4 ELF/map/最终镜像包含目标
  ↓
E5 板端执行路径可观察
  ↓
E6 行为可重复，且失败/恢复边界明确
```

证据只能支持它所在层及更低要求的问题。例如：

- `E1` 可以说明“这个 commit 中有实现”，不能说明“当前固件正在运行它”。
- `E3` 可以说明“某次构建成功”，不能说明 post-build 后最终镜像仍包含目标。
- `E5` 的一条日志可以说明“至少观察到一次”，不自动等于稳定、完整或无副作用。
- 教学图通常只帮助理解 `E0`～`E1` 的结构，本身不提升证据等级。

## 4. 四类入口各自负责什么

### 4.1 学习入口：`$LEARN`

用于概念解释、阅读顺序、只读练习、术语和人工整理的图。它回答“怎样学”，不回答“现在该实施什么”。

### 4.2 实施状态入口：`$IMPL`

- [`$IMPL/README.md`](../../../bk7258-t5ai/README.md)：实施文档总入口。
- [`$IMPL/next-stage-prompt.md`](../../../bk7258-t5ai/next-stage-prompt.md)：恢复指针，应继续指向当前 worklog 和最小下一步。
- `$IMPL/nuttx-port/`、`$IMPL/bootloader/` 等：保存设计、调查、验证和阶段证据。

学习文档只链接这些入口，不复制 current 状态表。

### 4.3 队伍源码入口：`$BOARD`

[`$BOARD`](../../../../board/bk7258/)是队伍拥有的 BK7258 board overlay。阅读函数前先记录：

1. 实际 Git 仓根；
2. 分支；
3. commit；
4. 文件是否有未提交变化；
5. 本次讲解使用 committed snapshot 还是工作树内容。

如果另一个会话正在修改工作树，教学取材应优先使用明确的 committed checkpoint，避免把半完成状态写进教程。

### 4.4 上游、外部资料与构建证据

`$WORKSPACE/nuttx`、`$WORKSPACE/apps`、芯片手册、外部 SDK、`.config`、ELF、map、bin 和板端日志都有独立版本。跨来源讲解时，应分别记录每个来源的 ref/commit/版本，而不是只写一个日期。

## 5. 来源冲突时的处理顺序

遇到两个文档或两份代码不一致时，不要立即挑一个“看起来更新”的版本：

1. **确认问题类型。** 是 current 状态、接口语义、源码实现、构建结果还是板端行为？
2. **确认所有者。** 找到真正维护该事实的仓库或文档入口。
3. **固定版本。** 记录 branch、commit、配置、产物哈希或外部文档版本。
4. **比较证据等级。** 源码结论不能覆盖更高层的反向板端证据；旧板测也不能证明新镜像。
5. **保留冲突。** 证据不足时写“待确认”，不要把两份材料拼成一个未经验证的新结论。
6. **只设计一个最小验证。** 进入构建、烧录或板测前，仍需单独实施授权。
7. **事实先更新实施区。** implementation truth 改变后，再同步教学解释；顺序不能反过来。

## 6. 每篇源码讲解的来源头

未来讲解具体文件和函数时，至少使用下面模板：

```markdown
> **来源记录**
>
> - 教学主题：
> - source repository：
> - source branch/ref：
> - source commit：
> - working-tree 状态：clean / dirty（本文是否使用未提交内容）
> - 相关 Kconfig / `.config`：
> - 构建后端：Make / CMake / 待确认
> - 上游或外部文档版本：
> - 最后核对日期：
> - 教学省略项：
> - 尚未证明：
```

如果无法填写 source commit，文章只能标为“临时笔记”，不能进入完成课程索引。

## 7. Graphify 应处在证据链的什么位置

Graphify 可以帮助发现：

- 可能的重要文件、函数和调用边；
- 高连接度节点或异常连接；
- 下一步值得人工核对的问题。

但它不能单独证明：

- 宏和条件编译在当前 `.config` 下的真实结果；
- 链接器垃圾回收后哪些符号仍在最终镜像；
- 函数指针、注册表、汇编入口和硬件回调的全部动态边；
- 板端真实执行顺序；
- 一条实现已经构建或板测通过。

因此原始 `graphify-out/` 保留在工作区缓存，教学区只收录经过源码复核并带 source commit 的精选图。具体规则见 [Graphify 安全使用约定](../assets/graphify/README.md)和[教学图索引](../90-reference/95-graph-index.md)。

## 8. 新手判断题

读完后，尝试判断：

1. `defconfig` 中出现某符号，是否足以证明最终 `.config` 已启用？
2. Graphify 显示函数 A 调用函数 B，是否足以证明板上一定走到 B？
3. 某次串口日志出现成功标志，是否足以证明另一份镜像也成功？
4. 映射目标目录中能看到源文件，是否说明应该直接在那里编辑？
5. 实施 worklog 与教程不一致时，应该先修改哪一边？

推荐答案依次是：否、否、否、否、先核对并更新实施事实，再更新教学解释。
