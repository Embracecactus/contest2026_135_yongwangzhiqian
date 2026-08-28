# Feature Trace API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1724&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:53  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/feature/feature_framework_trace.md) | 简体中文 \]

# Feature Trace API

Feature 框架中用于性能追踪（trace）打点的宏定义。这些宏在启用时调用 NuttX 的 <span class="reference">sched\_note</span> 接口记录事件，未启用时展开为空操作。

头文件：<span class="reference">\#include \<feature\_trace.h\></span>

# openvela 实现说明

  - **条件编译**：所有 trace 宏由 <span class="reference">CONFIG\_FEATURE\_USE\_SCHED\_NOTE</span> 配置项控制
      - 启用时，展开为 <span class="reference">sched\_note\_\*</span> 系列调用，使用 <span class="reference">NOTE\_TAG\_ALWAYS</span> 标签
      - 未启用时，展开为空操作（不产生任何 CPU / 内存开销），适合生产环境编译
  - **依赖**：依赖 NuttX 内核的 <span class="reference">sched\_note</span> 机制，需同时启用 <span class="reference">CONFIG\_SCHED\_INSTRUMENTATION</span> 相关配置
  - **使用场景**：在 Feature 接口实现或 JS-Native 边界处打点，配合 openvela 的 trace 分析工具（如 SystemView、Perfetto）可视化性能瓶颈
  - **成对使用**：<span class="reference">FEATURE\_NOTE\_BEGIN\*</span> / <span class="reference">FEATURE\_NOTE\_END\*</span> 必须成对调用，否则 trace 事件配对会失败

# 基础打点宏

## FEATURE\_NOTE\_PRINTF

    FEATURE_NOTE_PRINTF(format, ...)

以格式化字符串打点，类似 <span class="reference">printf</span>。用于记录自定义调试信息。

**参数**：

  - <span class="reference">format</span> 格式化字符串。
  - <span class="reference">...</span> 可变参数列表，与 format 占位符对应。

## FEATURE\_NOTE\_BEGIN

    FEATURE_NOTE_BEGIN()

标记一段代码执行的开始（无附加信息）。必须与 <span class="reference">FEATURE\_NOTE\_END</span> 成对使用。

## FEATURE\_NOTE\_END

    FEATURE_NOTE_END()

标记一段代码执行的结束。与最近一次 <span class="reference">FEATURE\_NOTE\_BEGIN</span> 配对。

# 带标签的打点宏

## FEATURE\_NOTE\_BEGIN\_STR

    FEATURE_NOTE_BEGIN_STR(str)

带字符串标签的起始打点，用于标识代码段的语义。

**参数**：

  - <span class="reference">str</span> 事件标签字符串，该字符串需在整个 trace 事件期间保持有效。

## FEATURE\_NOTE\_END\_STR

    FEATURE_NOTE_END_STR(str)

带字符串标签的结束打点，与对应 <span class="reference">FEATURE\_NOTE\_BEGIN\_STR</span> 的标签一致。

**参数**：

  - <span class="reference">str</span> 事件标签字符串（必须与起始打点的标签一致）。

## FEATURE\_NOTE\_MARK

    FEATURE_NOTE_MARK(str)

打一个即时标记点，不需要配对。用于在时间线上标记单一事件。

**参数**：

  - <span class="reference">str</span> 标记标签字符串。

# 作用域打点宏

## FEATURE\_NOTE\_BEGIN\_LOCAL / FEATURE\_NOTE\_END\_LOCAL

    FEATURE_NOTE_BEGIN_LOCAL(str)
        // 被追踪的代码
    FEATURE_NOTE_END_LOCAL()

带局部变量作用域的起止打点。内部通过局部变量保存标签，避免上层代码传参复杂。

**参数**：

  - <span class="reference">str</span> 事件标签字符串。

**使用示例**：  

    void my_feature_func(void)
    {
        FEATURE_NOTE_BEGIN_LOCAL("my_feature_func");
        // ... 业务逻辑 ...
        FEATURE_NOTE_END_LOCAL();
    }

**注意**：

  - <span class="reference">FEATURE\_NOTE\_BEGIN\_LOCAL</span> 与 <span class="reference">FEATURE\_NOTE\_END\_LOCAL</span> 必须在同一作用域内成对使用，宏内部使用 <span class="reference">do { ... } while(0)</span> 模式封装，依赖编译器能够识别作用域。
  - 宏内部会引入名为 <span class="reference">note\_temp\_str</span> 的局部变量，同一作用域内不要使用该变量名。
