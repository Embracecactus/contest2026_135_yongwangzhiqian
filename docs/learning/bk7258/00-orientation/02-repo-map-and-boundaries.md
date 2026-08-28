# 02｜仓库地图与边界

初学者最容易犯的错误，不是某一行 C 写错，而是**在错误的仓库、错误的映射入口或错误的证据层上工作**。本篇先把文件放回它们各自的所有者和职责中。

> **来源记录**
>
> - 教学主题：openvela 多仓工作区、contest overlay、BK7258 board tree 与编辑边界
> - source ref：`$CONTEST` 的 `HEAD`（撰写时）
> - source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 最后核对日期：2026-07-24
> - 直接来源：[contest README](../../../../README.md)、[contest manifest](../../../../contest2026_135_yongwangzhiqian.xml)、[`$BOARD`](../../../../boards/bk7258/)
> - 教学简化：目录图只展示理解所有权所需的主干，不表示每个文件都会在当前配置中编译

## 1. 路径变量

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export IMPL="$CONTEST/docs/platforms/bk7258"
export LEARN="$CONTEST/docs/learning/bk7258"
export BOARD="$CONTEST/boards/bk7258"
```

不要把示例中的变量替换成写进文档的个人绝对路径。命令可以在本地展开变量，文档应保留变量形式。

## 2. `$WORKSPACE` 是多仓工作区，不是单仓库

`repo init` / `repo sync` 后的概念结构如下：

```text
$WORKSPACE/
├── .repo/                         # repo 元数据与 manifest 管理区
├── nuttx/                         # 一个上游/基础项目仓库
├── apps/                          # 一个上游/基础项目仓库
├── packages/                      # 完整构建树中的包入口
├── vendor/                        # 完整构建树中的板级入口
├── frameworks/ external/ ...      # 其他 repo 项目
├── prebuilts/                     # 预编译工具或依赖
└── contest2026_135_yongwangzhiqian/
    └── ...                        # 队伍拥有的 contest 仓库，即 $CONTEST
```

关键认识：

- `$WORKSPACE` 根目录由 `repo` 组织多个 Git 项目；根本身未必是一个可统一提交的 Git 仓库。
- `git status`、`git log`、`git diff` 应在具体拥有者仓库执行，例如 `git -C "$CONTEST" ...`。
- `.repo/` 管理工作区结构，不是教学扫描、普通编辑或提交内容的目标。
- `prebuilts/` 通常体积大且不是理解队伍实现的首选来源，也不应进入教学图扫描范围。

## 3. contest overlay 与 manifest 映射

[contest manifest](../../../../contest2026_135_yongwangzhiqian.xml)把队伍拥有的目录暴露给完整 openvela 构建树。与 BK7258 board 相关的映射是：

```text
源：$CONTEST/boards/bk7258
目的：$WORKSPACE/vendor/openvela/boards/contest2026_135_bk7258
```

可以把它理解成：

```text
队伍拥有并提交的源目录
$BOARD
   │
   │ manifest <linkfile>
   ▼
完整工作区给构建系统看到的入口
$WORKSPACE/vendor/openvela/boards/contest2026_135_bk7258
```

### 所有权规则

- **修改队伍 board：**回到 `$BOARD` 修改并从 `$CONTEST` 提交。
- **查看构建视角：**可以只读检查 manifest 目标是否正确指向队伍源。
- **不要双写：**不要把映射目标当成第二份独立源码再改一次。
- **公共仓改动：**如果问题确实属于 `$WORKSPACE/nuttx` 等公共项目，应走该项目自己的上游分支/PR 流程，不能把公共仓修改伪装成 contest overlay 改动。

## 4. 三区隔离的文件系统落点

### 区一：学习区

```text
$LEARN/
├── README.md
├── 00-orientation/
├── 90-reference/
└── assets/
```

职责：概念、路线、只读练习、来源模板和人工整理的教学图。

### 区二：实施区

```text
$IMPL/                         # 实施文档、worklog、current pointer
$BOARD/                        # 队伍 BK7258 board overlay 源码
$CONTEST/contest2026_135_yongwangzhiqian.xml
```

职责：当前实现、配置、验证记录、恢复入口和队伍拥有的映射定义。

### 区三：工作区与参考区

```text
$WORKSPACE/nuttx/
$WORKSPACE/apps/
$WORKSPACE/packages/
$WORKSPACE/vendor/
外部 SDK、芯片手册、工具链、构建目录与产物
```

职责：上游实现、外部规范、构建输入、映射视图和验证产物。每个来源都有自己的所有者和版本，不能笼统写成“工作区最新代码”。

## 5. `$BOARD` 主干地图

以下结构来自撰写基线的 tracked tree，只用于说明职责。某个文件是否真正生效，仍要继续核对 Kconfig、有效 `.config`、当前构建后端和最终产物。

```text
$BOARD/
├── CMakeLists.txt                 # board 级 CMake 入口与链接脚本入口
├── Kconfig                        # board 选择/配置入口
├── configs/
│   └── nsh/
│       └── defconfig              # 一个 board configuration 的请求值
├── include/
│   └── board.h                    # board 对外声明
├── scripts/
│   ├── Make.defs                  # classic Make 集成入口之一
│   ├── ld.script                  # 链接布局
│   ├── postbuild.sh               # 链接后处理/打包入口之一
│   └── ...                        # SDK 安装/校验等辅助脚本
├── bootloader/
│   ├── README.md
│   ├── start.S
│   ├── boot_main.c
│   ├── bootloader.ld
│   └── ...                        # bootloader 构建与打包材料
├── chip/
│   ├── Kconfig
│   ├── Make.defs
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── bk7258_irq.h               # IRQ 编号与 NR_IRQS 定义
│   │   └── bk7258_amp.h               # AP/多核 mailbox、doorbell 接口
│   └── bk7258_*.c / bk7258_*.S / *.h  # 芯片级实现
│       ├── bk7258_start.S              # 复位入口、向量表
│       ├── bk7258_serial.c             # UART/console 驱动
│       ├── bk7258_gpio_lowerhalf.c     # GPIO user-space /dev/gpioN 驱动
│       ├── bk7258_sdk_irq.c            # SDK IRQ bridge：source↔NuttX 映射
│       ├── bk7258_wdt.c                # 看门狗驱动
│       ├── bk7258_ap_control.c         # CPU1 AP 控制接口
│       ├── bk7258_ap_start.c           # CPU1 启动逻辑
│       ├── bk7258_ap_main.c            # CPU1 AP NuttX 入口
│       ├── bk7258_ap_vectors.c         # CPU1 专用向量表
│       └── ...                         # 时钟、存储、计时器等
├── src/
│   ├── Makefile
│   ├── CMakeLists.txt
│   └── bk7258_bringup.c           # board bring-up 层
└── bk_idk/
    └── README.md                  # 本地集成材料的边界说明入口
```

### 怎样读这棵树

- `Kconfig` 回答“有哪些可选项”，不单独证明某项已经启用。
- `defconfig` 回答“这个 board config 请求了什么”，生成后的 `.config` 才回答一次具体构建的有效值。
- `Make.defs` 与 `CMakeLists.txt` 属于不同构建后端；必须先确认当前构建实际走哪条路径。
- `ld.script` 约束链接布局；最终 ELF/map 和 post-build 产物用于确认实际结果。
- `chip/` 与 `src/` 分别偏向芯片层和板级 bring-up，但真实边界应以符号、调用关系和 NuttX 接口为准。
- `bootloader/` 与 NuttX 应用镜像处在同一启动链的不同阶段，不能因为都在 board 目录就把它们当成同一个链接目标。
- 目录名、注释和 README 可能保留历史阶段描述；它们是线索，不自动成为 current truth。

## 6. 实施文档入口与学习文档入口不能混用

| 问题 | 应先看哪里 |
|---|---|
| 我是新手，应该先学什么？ | [`$LEARN/README.md`](../README.md) 与[学习路线](01-learning-roadmap.md) |
| 当前移植状态是什么？ | [`$IMPL/README.md`](../../../platforms/bk7258/README.md) |
| 当前恢复入口在哪里？ | [`$IMPL/next-stage-prompt.md`](../../../platforms/bk7258/next-stage-prompt.md) |
| 队伍实现源码在哪里？ | [`$BOARD`](../../../../boards/bk7258/) |
| board 怎样进入完整构建树？ | [contest manifest](../../../../contest2026_135_yongwangzhiqian.xml) |
| 某个结论的权威证据是什么？ | [权威来源地图](03-authoritative-source-map.md) |

学习材料只链接实施入口，不复制 current 表格、当前门禁或下一动作。这样实施状态变化时，不需要在两套文档中同步同一事实。

## 7. 只读检查清单

以下命令用于建立上下文，不修改源码：

```bash
# 1. 队伍仓库状态
git -C "$CONTEST" status --short --branch

# 2. 队伍仓库基线 commit
git -C "$CONTEST" rev-parse HEAD

# 3. 查看 manifest 中由队伍拥有的路径
git -C "$CONTEST" ls-files \
  'contest2026_135_yongwangzhiqian.xml' \
  'boards/bk7258/**' \
  'docs/platforms/bk7258/**' \
  'docs/learning/**'

# 4. 只读观察映射目标本身
ls -ld "$WORKSPACE/vendor/openvela/boards/contest2026_135_bk7258"
readlink "$WORKSPACE/vendor/openvela/boards/contest2026_135_bk7258" || true
```

`readlink` 没有输出并不自动说明映射错误；不同工作区状态或工具实现可能需要结合 manifest 和文件身份继续核对。不要为了“让路径看起来对”而直接重建或修改映射，除非进入明确的实施任务。

## 8. 编辑边界速查表

| 路径/内容 | 默认动作 | 原因 |
|---|---|---|
| `$LEARN/**` | 在教学任务中编辑 | 教学区的唯一职责 |
| `$IMPL/**` | 只在实施事实变化时编辑 | 避免教学任务改写 current 状态 |
| `$BOARD/**` | 只在明确实现任务中编辑 | 队伍 BSP 源码，改动需要构建/产物/板端证据 |
| `$WORKSPACE/vendor/.../contest2026_135_bk7258` | 只读映射视图 | 真正拥有者是 `$BOARD` |
| `$WORKSPACE/nuttx/**`、`$WORKSPACE/apps/**` | 默认只读；需要时走对应上游流程 | 属于公共项目，不是 contest overlay |
| `$WORKSPACE/.repo/**` | 不编辑、不扫描 | repo 管理元数据 |
| `$WORKSPACE/prebuilts/**` | 不作为普通教学扫描输入 | 体积大、来源不同、容易制造噪声 |
| `.config`、ELF、map、bin、obj、archive | 作为一次构建的证据，不作为手写源文件提交 | 可再生且与具体构建绑定 |
| `$CONTEST/logs/**` | 不用于教学图扫描或改写 | AI Coding 日志有独立提交规范 |
| 密钥、token、凭据、`.env` | 永不采集、永不入图、永不提交 | 安全边界 |

## 9. 一个问题应该怎样定位

以“某个驱动为什么没有进入固件”为例，正确的定位顺序是：

```text
它属于哪个拥有者仓库？
  → 哪个 Kconfig 符号控制？
  → defconfig 请求了什么？
  → 生成 .config 实际是什么？
  → 当前 Make/CMake 后端是否加入源文件？
  → 对象是否生成？
  → ELF/map 是否包含符号？
  → post-build 是否保留到最终镜像？
```

不要直接在多个仓库同时改文件。先找到第一个断点，再决定是否需要进入实施流程。
