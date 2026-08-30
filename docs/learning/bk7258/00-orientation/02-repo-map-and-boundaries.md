# 02｜仓库地图与所有权边界

OpenVela 工作区由 repo manifest 组合多个 Git 项目。相同文件可以同时以“队伍仓真源”和
“官方目录中的 linkfile 视图”出现，但所有权仍只有一处；不能在两个位置各维护一份。

## 1. 当前队伍仓结构

```text
contest2026_135_yongwangzhiqian/
├── app/
│   ├── hello_app/              # 系统生成的原始示例
│   ├── bk7258/                 # 队伍自有 BK7258 命令应用
│   └── testing/bk7258/         # 可上游的 target CMocka 应用
├── quickapp/hello_quickapp/    # 系统生成的原始快应用示例
├── chips/bk7258/               # 三板共享的 SoC 机制
├── boards/bk7258/
│   ├── common/                 # 三板共享的板级基础设施
│   ├── t5_board/               # T5-Board 物理策略
│   ├── t5ai_core/              # T5AI-Core 物理策略
│   ├── aidk_ai_toy/            # AIDK AI Toy 物理策略
│   └── build/                  # 必需的 vendor build hook
├── nuttx/                      # 可上游通用驱动的镜像 overlay
├── tests/
│   ├── host/bk7258/            # Linux 原生 mock/sanitizer 回归
│   └── pytest/test_bk7258/     # 官方 pytest runner 的板级子目录
├── tools/bk7258/               # 唯一公开 BK7258 CLI 及内部库
├── docs/                       # 契约、学习材料和日期化证据
└── contest2026_135_yongwangzhiqian.xml
```

`app/hello_app` 和 `quickapp/hello_quickapp` 保持模板职责。产品命令位于独立
`app/bk7258`，避免示例、产品功能和验证入口再次混为一体。

## 2. manifest 映射

队伍 manifest 以目录为单位暴露真源：

| 队伍仓源目录 | 工作区目标 | 用途 |
|---|---|---|
| `app/hello_app` | `packages/demos/contest2026_135_hello_app` | 原始 native 示例 |
| `app/bk7258` | `apps/system/bk7258` | BK7258 产品/验证命令 |
| `quickapp/hello_quickapp` | `packages/apps/contest2026_135_hello_quickapp` | 原始 QuickApp 示例 |
| `chips/bk7258` | `vendor/beken/chips/bk7258` | NuttX custom chip |
| `boards/bk7258` | `vendor/beken/boards/bk7258` | 三块 custom board |
| `nuttx` | `vendor/beken/nuttx` | 上游形态的通用驱动 overlay |
| `app/testing/bk7258` | `apps/testing/bk7258` | target 测试应用 |
| `tests/pytest/test_bk7258` | `tests/scripts/script/test_bk7258` | 官方 pytest 子测试 |
| `prebuilt` | `vendor/beken/prebuilt` | 锁定工具链 |
| `tools/bk7258` | `vendor/openvela/tools/contest2026_135_bk7258` | 工作区工具视图 |

这些目标是符号链接视图，不是新的代码所有者。需要提交的修改必须落回队伍仓源目录。

## 3. 分层判断

遇到新功能时先判断所有者：

- 芯片控制器、IRQ、DMA、跨核机制和通用传输能力属于 `chips/bk7258/`。
- GPIO 编号、供电、引脚复用、器件数量和实例注册属于相应 `boards/bk7258/<board>/`。
- 与 BK7258 无关的通用外设协议驱动属于 `nuttx/drivers/<class>/` 形态的队伍 overlay。
- NuttX/OpenVela 已有驱动时只补 chip lower half 和 board binding，不复制通用驱动。
- 产品命令属于 `app/bk7258/`；host、target 和 serial 自动化分别使用三个测试目录。

## 4. 路径与构建入口

构建代码使用官方提供的 `TOPDIR`、`APPDIR`、`NUTTX_DIR`、`NUTTX_BOARD_DIR` 或
`CMAKE_CURRENT_LIST_DIR`。独立脚本可以从自身位置解析一次根目录，后续都使用命名变量；
不在多个规则中重复 `../../../`，也不写本机绝对路径。

文档同一子树内使用相对链接。跨到源码或根配置时写仓库逻辑路径，例如
`boards/bk7258/CONFIGS.md`，避免文档层级变化后出现大量失效导航。

## 5. 只读核对

```bash
cd "$CONTEST"
git status --short

# manifest 的队伍映射
sed -n '/<project path="contest2026_135_yongwangzhiqian"/,/<\/project>/p' \
  contest2026_135_yongwangzhiqian.xml

# 工作区映射必须指回队伍仓
readlink "$WORKSPACE/vendor/beken/chips/bk7258"
readlink "$WORKSPACE/vendor/beken/boards/bk7258"
readlink "$WORKSPACE/apps/testing/bk7258"

# 官方仓不应出现队伍拥有的 tracked 修改
git -C "$WORKSPACE/nuttx" status --short
git -C "$WORKSPACE/apps" status --short
git -C "$WORKSPACE/tests" status --short
```

未跟踪内容不应直接当作队伍改动删除；先判断它是 linkfile、官方仓构建产物还是用户另行
保留的材料。只有所有权和用途确定后才清理。

## 6. 阅读出口

读完后应能回答：一个文件由哪个 Git 仓拥有、manifest 把它暴露到哪里、它属于 chip、
board、通用驱动还是应用层，以及要用哪一类构建/产物/板端证据证明它真正生效。
