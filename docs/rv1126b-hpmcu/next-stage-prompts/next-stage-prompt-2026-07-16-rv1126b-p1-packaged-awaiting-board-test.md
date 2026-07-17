# 下一阶段恢复提示词：RV1126B P1 已构建并打包，等待板端验证

在上下文重置后使用本文档继续推进 RV1126B HPMCU openvela / NuttX contest overlay。

```text
我们正在 openvela 2026 竞赛工作区继续工作。当前目标：P1 收敛代码已经实现，
classic Make 构建和 AMP FIT 打包已经通过；用户将自行刷写 AMP 分区。下一阶段应等待
用户提供实际刷写命令、刷写输出和板端串口 transcript，再归档当前 P1 candidate 的
runtime 验证证据。

严格交互规则：

- 使用苏格拉底式沟通：如果范围或意图不清楚，先停下来问我，不要自行扩展范围。
- 不要主动加载 skills，除非我明确要求。尤其不要自动运行 build / flash / SDK packaging 相关 skill。
- 本项目不要使用 Workflow。
- 主模型保持在规划和复核角色；重复检查、机械验证可在实际需要且范围明确时委托普通 Agent。
- 如果主模型可能超时，可使用 Sonnet/Haiku Agent 继续已明确的机械执行或验证，避免中断任务。
- 只修改 `$CONTEST` 团队仓库。不要直接修改外层官方 checkout，例如 `$WORKSPACE/nuttx/`、
  `$WORKSPACE/apps/`、`$WORKSPACE/packages/`、`$WORKSPACE/vendor/`、`$WORKSPACE/frameworks/`。
- SDK 下的 build/package 产物仅在我明确授权时覆盖；任何覆盖前先检查目标并列出旧 hash。
- 使用 CodeGraph 前先运行 `codegraph sync`；然后调用 CodeGraph 时使用 `projectPath: "$WORKSPACE"`。
  如果 sync 或 CodeGraph 不可用，先说明 fallback，再进行手工读取/搜索。
- 未经确认，不要运行长构建、SDK 打包、刷机、push、创建 PR、删除、reset 或覆盖性操作。
- 本阶段用户明确选择“自己刷机”。Claude 不得调用 `upgrade_tool`、`rkdeveloptool` 或其他刷写命令。
- 文档中不要写入个人绝对路径。使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。
- README 在我明确说进入最终项目 README 阶段前，应尽量保持接近官方模板。

路径约定：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$OUT/firmware"
```

实际 `$SDK` 绝对路径由用户在上一会话中提供，但没有写入仓库文档。上下文恢复后若环境变量未设置，
先请用户重新提供或确认；不要自行猜测路径。

当前 Git / PR 上下文：

- 团队仓库：`$CONTEST`
- 分支：`submit-rv1126b-nsh-baseline`
- 跟踪远端：`fork/submit-rv1126b-nsh-baseline`
- 当前 HEAD：`1c42588 docs: remove signed CLA from next steps, update status`
- PR 已提交到官方仓库，CLA 已通过，等待 reviewer 反馈。
- 本轮 P1 变更尚未 commit/push，因此尚未进入远端 PR。
- 当前 tracked diff：14 个文件，81 行新增、29 行删除。
- 当前另有 3 个预期 untracked docs：两份 verification 记录和本恢复提示词。
- 不要自动 commit 或 push。提交前必须检查新增 AI logs，并询问用户。

会话开始只读检查：

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" diff --check
git -C "$CONTEST" diff --stat
git -C "$CONTEST" status --short -- logs docs
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

如需要核对 SDK 产物，并且用户已经重新确认 `$SDK`：

```bash
readlink -f "$OUT/rtt.bin"
sha256sum "$WORKSPACE/nuttx/nuttx.bin" "$FW/amp.img"
```

当前 AI logs 状态（写本文档前）：

```text
Files checked:  94
Events checked: 26069
ALL OK
```

本轮长会话本身可能尚未归集到 `$CONTEST/logs/`。提交或推送前先问用户是否运行日志归集；
不要自动加载或运行日志 skill。

本轮已经完成的 P1 代码收敛：

1. MTIME tick 跟随 NuttX 配置：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - 使用 `<nuttx/clock.h>` 的 `TICK_PER_SEC`，不再固定除以 100。
   - divider 后有效频率为 396 kHz；默认 `CONFIG_USEC_PER_TICK=10000` 时仍为 3960 counts/tick。
   - 增加编译期 guard，防止 tick rate 非法或 compare step 截断为 0。
   - 未改变 MTIME high-low-high 读取、MTIMECMP 安全写入、IRQ attach/dispatch 路径。

2. GPLL 失败路径可见：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_clockconfig.c`
   - GPLL 未锁定时通过 `riscv_lowputc()` 输出：`WARNING: GPLL not locked`。
   - 告警后保持寄存器不变并继续启动；不 PANIC、不由 HPMCU 重配 A-core/bootrom 所有的时钟。
   - 修正 GPLL lock 注释：`GPLL_CON[2]` offset `0x28`、bit 10。

3. Console 符号收敛：
   - 文件：`$CONTEST/board/contest_board/chip/rv1126b_serial.c`
   - `g_console_port` 改为 `static`；所有引用仍在同文件内。

4. Team 135 template 归一化：
   - `app/hello_app/` 的 Kconfig/Make/CMake/README/展示文本从 team 000 改为 team 135。
   - Kconfig symbol：`LVX_USE_DEMO_CONTEST2026_135_HELLO_APP`。
   - APPDIR target：`packages/demos/contest2026_135_hello_app`。
   - QuickApp package：`com.openvela.contest2026.team135`。
   - QuickApp mapping：`packages/apps/contest2026_135_hello_quickapp`。
   - sample 目录内 `contest2026_000` / `team000` / `team 000` 已清零。

5. 文档一致性：
   - `board/contest_board/README.md` 已修正 UART5 IRQ namespace：raw INTMUX source 61 在软件侧转换为
     `RV1126B_IRQ_UART5 = RISCV_IRQ_EXT + 61`。
   - `logs/README.md` 已改为真实日志目录状态。
   - 根 `README.md` 保持官方模板，未进入最终 README 阶段。

6. GPIO3 暂缓：
   - `rv1126b_memorymap.h` 定义 GPIO3 为 `0x21E00000`。
   - `rv1126b_gpio.h` 定义 GPIO3 为 `0x21C00000`。
   - 当前 BSP 未使用 GPIO3。本轮没有猜地址或修改宏；开发 GPIO3 driver 前必须依据 SDK/TRM 核实。

classic Make 构建结果：

命令（从 `$WORKSPACE`）：

```bash
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

结果：exit code 0。

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$WORKSPACE/nuttx/nuttx` | 182916 bytes | `378c8a78f0625e03f2d5911e4bbb7cff1e4c2f375ca060588f8c89f69462f494` |
| `$WORKSPACE/nuttx/nuttx.bin` | 98820 bytes | `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00` |
| `$WORKSPACE/nuttx/nuttx.hex` | 278009 bytes | `8d2812cc6a4c94b683eef7253b49cac5e76e75dfb271443e8d041287f9c9f66b` |

RAM：104832 / 237568 bytes（44.13%）。

唯一 warning：

```text
riscv-none-elf-ld: warning: $WORKSPACE/nuttx/nuttx has a LOAD segment with RWX permissions
```

warning 未导致失败。Build 非确定性；hash 只标识本次 artifact，不证明可复现性。

AMP FIT 打包结果：

- 使用的是 Rockchip SDK 自带的 ELF 可执行文件，不是系统/U-Boot 的同名工具：

```text
$SDK/rtos/bsp/rockchip/tools/mkimage
```

- 版本：`mkimage version 2017.09-g5f647be153-210721-dirty #stevenliu`
- 输入 ITS：`$OUT/amp.its`
- `$OUT/rtt.bin` 保持 symlink：

```text
../rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin
```

- symlink 的真实 target 已替换为本次 `$WORKSPACE/nuttx/nuttx.bin`，替换后 hash 相同：
  `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00`。

打包命令（从 `$OUT`）：

```bash
$SDK/rtos/bsp/rockchip/tools/mkimage \
  -f amp.its \
  -E \
  -p 0xe00 \
  "$FW/amp.img"
```

结果：exit code 0。

关键 FIT 信息：

```text
FIT description: FIT source file for rockchip AMP
Image 0 (hpmcu)
Data Size:    98820 Bytes = 96.50 KiB
Architecture: ARM
Load Address: 0x48c02000
Hash algo:    sha256
Hash value:   26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00
Loadables:    hpmcu
```

`Architecture: ARM` 是 Rockchip FIT/boot chain 的既有封装约定；payload 是 RV1126B HPMCU RISC-V
`nuttx.bin`，不要误称 payload 为 ARM。

新 AMP 产物：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$FW/amp.img` | 103936 bytes | `585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9` |

同一 `mkimage -l` 检查通过，内容与打包输出一致。

覆盖前已经创建备份：

| 备份 | 原产物 sha256 |
| --- | --- |
| `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin.pre-p1-20260716` | `63286394f011246de1669da5016389ee5c5e6f74da577d54d594465c665590ca` |
| `$FW/amp.img.pre-p1-20260716` | `f82b910e3d72e58c4f3e2dccb3a67f9ed5e740ffa209ebc188630e1b149f8dc7` |

本轮明确未执行：

- `update.img` 重新打包；
- `upgrade_tool` / `rkdeveloptool`；
- 任何 flash；
- 板端 NSH / UART / `help` / `uname -a` / `ps` 验证。

当前验证证据文档：

```text
$CONTEST/docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-convergence-build.md
$CONTEST/docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-amp-package.md
```

当前证据结论：

- 当前 P1 working-tree candidate：classic Make build 已验证。
- 当前 P1 `amp.img`：Rockchip FIT packaging 已验证。
- 当前 P1 `amp.img` 的 flash/runtime 尚未验证。
- 较早恢复提示词报告 P0/P1 修复后做过板测，但已提交 JSONL/docs 中没有绑定 `dc9b8ed` 或当前
  `amp.img` hash 的独立 build/flash/NSH transcript。不能复用旧 artifact hash 代表当前候选。

用户下一步动作：

用户明确选择自己执行刷机。Claude 应等待用户贴出结果，不得主动刷写。

用户刷写前应核对：

```bash
sha256sum "$FW/amp.img"
```

预期：

```text
585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9
```

用户应使用自己已经确认的命令只刷写 AMP 分区，不要猜测命令，不要默认 full `update.img`。

板端串口参数：

```text
UART5 M0
1500000 baud
8N1
```

用户刷写后应提供：

1. 实际刷写命令及完整输出；
2. 完整启动日志；
3. NSH prompt；
4. `help` 输出；
5. `uname -a` 输出；
6. `ps` 输出；
7. UART RX/TX 交互证据；
8. 是否出现 `WARNING: GPLL not locked`。

如果出现 GPLL warning：只记录完整输出并停止扩大操作；不要尝试由 HPMCU 重配时钟。

如果新镜像启动失败：

- 不要自动刷回或覆盖任何文件；先与用户确认。
- 可供用户回滚的旧 AMP image：`$FW/amp.img.pre-p1-20260716`。
- SDK 原 RTT target 备份：
  `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin.pre-p1-20260716`。

如果板端验证通过：

1. 新增一份与当前 `amp.img` hash 绑定的 runtime verification 文档，例如：
   `docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-board-runtime.md`。
2. 只记录用户实际提供的 flash command/output、串口 transcript 和产物 hash；不得补写或猜测。
3. 更新 `board/contest_board/README.md`，把当前 P1 candidate 从 build+packaged 提升为 board-verified。
4. 运行 `git diff --check` 和 AI log validator。
5. 在 commit/push 前检查是否新增 logs，并询问用户。

当前不可声称：

- 当前 P1 `amp.img` 已经刷写或板测通过；
- CMake 等价；
- DCache 已启用/已验证；
- RPMsg / Linux A-core 到 HPMCU 通信已完成；
- full `update.img` flashing 已通过；
- GPIO3 地址已经确认。

提交策略（只有用户明确授权后）：

- 先检查并归集当前长会话 AI logs。
- 建议把代码/元数据收敛与验证文档拆成清晰提交；不要自动 commit。
- push 前重新检查 PR/remote 状态和 reviewer 反馈。
- 如果 reviewer 要求修改外层 `$WORKSPACE/nuttx/`，停止并讨论跨仓库处理方式。
```

## 本上下文窗口变更摘要

### 当前 `$CONTEST` tracked 修改

```text
app/hello_app/CMakeLists.txt
app/hello_app/Kconfig
app/hello_app/Make.defs
app/hello_app/Makefile
app/hello_app/README.md
app/hello_app/hello_app_main.c
board/contest_board/README.md
board/contest_board/chip/rv1126b_clockconfig.c
board/contest_board/chip/rv1126b_serial.c
board/contest_board/chip/rv1126b_timerisr.c
logs/README.md
quickapp/hello_quickapp/README.md
quickapp/hello_quickapp/manifest.json
quickapp/hello_quickapp/src/pages/index/index.ux
```

### 当前新增文档

```text
docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-convergence-build.md
docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-amp-package.md
docs/rv1126b-hpmcu/next-stage-prompts/next-stage-prompt-2026-07-16-rv1126b-p1-packaged-awaiting-board-test.md
```

### 已通过检查

```text
codegraph sync（修改前和最终代码复核前）
git diff --check
sample team 000 占位搜索：0 个
QuickApp manifest JSON 解析
console symbol 引用检查
classic Make build
Rockchip mkimage FIT packaging
mkimage -l
AI log validation：94 files / 26069 events / ALL OK
```

未 commit、未 push、未 flash。
