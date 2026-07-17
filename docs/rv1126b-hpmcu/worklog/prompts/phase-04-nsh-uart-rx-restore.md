> [!WARNING]
> **Resolved / superseded historical prompt.** The RX investigation below predates the verified interrupt-driven UART5 M0/IPIC solution. Do not use its UART4, idle-loop polling, diagnostic-marker, or pending-root-cause instructions as current work. Start with the [Phase 05 verified-baseline follow-up](phase-05-verified-baseline-follow-up.md), the [canonical port guide](../../adaptation/nsh-port.md), and the immutable [2026-07-14 baseline evidence](../../verification/2026-07-14-rv1126b-nsh-baseline.md).

# Phase 04 -- RV1126B NSH 串口 RX 调试恢复提示词

> `/clear` 后直接粘贴以下内容给 Claude Code，即可恢复上下文。

---

我正在调试 RV1126B openvela NSH 串口 RX 失效问题。请严格按照以下信息恢复工作状态。

## 工作区与规则

- **工作区根目录**: `$WORKSPACE`
- **团队 overlay**: `$CONTEST`
- **RV1126B SDK**: `$SDK`
- **严格禁止**修改官方 `nuttx/`、`apps/`、`packages/`、非团队 `vendor/` checkout；所有修复只允许在团队 overlay 内，尤其是 `board/contest_board/`。
- **主模型只做规划和审查**；宽泛搜索、机械修改、编译、打包、验证全部交给 Sonnet 或 Haiku 子代理。
- 项目有 **CodeGraph**（`.codegraph/` 目录已初始化），理解/定位代码前优先使用 CodeGraph，projectPath 为 `$WORKSPACE`。
- **不使用 Workflow**；本项目分析用普通 Agent（Sonnet/Haiku）。

## 已完成工作

1. 原始 defconfig 只有 `CONFIG_SYSTEM_NSH=y`、`CONFIG_NSH_READLINE=y` 等，缺少 serial console 配置。
2. 已在团队 overlay 文件 `board/contest_board/configs/nsh/defconfig` 中添加：
   ```
   CONFIG_DEV_CONSOLE=y
   CONFIG_SERIAL=y
   CONFIG_UART_SERIALDRIVER=y
   ```
3. 已执行 distclean 并成功编译：
   ```bash
   ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
   ```
4. 输出 ELF：`$WORKSPACE/nuttx/nuttx`；objcopy 后 `nuttx.bin` 约 80680 bytes。
5. 已打包：
   - `amp.img`：SDK `output/firmware/amp.img`，约 84K
   - `update.img`：SDK `output/update/Image/update.img`，约 1.4G
6. **打包流程**必须包含以下步骤：
   1. `riscv-none-elf-objcopy -O binary nuttx nuttx.bin`
      （工具路径：`$WORKSPACE/prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-objcopy`）
   2. 复制 `nuttx.bin` 到 `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin`
   3. `$SDK/hal/tools/mkimage -f Image/amp.its -E -p 0xe00 Image/nuttx_amp.img`
   4. 复制为 `$SDK/output/firmware/amp.img`
   5. `$SDK/build.sh updateimg`

## 当前实板现象

烧录新 `update.img` 后串口输出：

```
ABCDABCDPEFGTtSs0e0bLbcle0b
NuttShell (NSH)
!!!
```

关键分析：
- **NSH banner 已输出**，TX 正常，`riscv_serialinit()` 很可能已执行且 `/dev/console` 注册成功。
- **键盘输入/回车完全无响应**，问题已缩小到 UART5 RX 链路：attach/rxint、IRQ 路由/INTMUX、IRQ 编号/dispatch、UART IIR/IER 宏，或 RX polling 缺失。
- **不要再把根因简单归为 `/dev/console` 未注册**；前一阶段已解决该问题。

## 已知团队代码文件

以下是下一步重点审查的文件（均在 `board/contest_board/` 下）：

- `chip/rv1126b_serial.c`
  - UART5 base，IRQ 暂定 `61`
  - `riscv_earlyserialinit()` setup
  - `riscv_serialinit()` 注册 `/dev/console`、`/dev/ttyS0`
  - ops 包含 setup/attach/rxint/receive/rxavailable/interrupt
- `src/rv1126b_boot.c`
  - `board_late_initialize()` 调用 `rv1126b_bringup()`
- `src/rv1126b_bringup.c`
  - 当前主要挂 procfs 和调试字符
- `chip/rv1126b_irq.c`
- `chip/rv1126b_irq_dispatch.c`
- `chip/hardware/rv1126b_intmux.h`
- `chip/include/irq.h`

## clear 前已启动的调查

已并行派出三个只读 Agent。如果 clear 后结果不可用，**重新派发**，不要主模型亲自扫文件：

1. **Sonnet（调查 A）**：映射所有调试字符（`ABCDABCDPEFGTtSs0e0bLbcle0b` 和 `!!!`），审查团队 UART RX 驱动和 IRQ/dispatch 实现。
2. **Haiku（调查 B）**：核对官方 NuttX serial upper-half / NSH open-read 调用链，只读官方代码，**禁止提出官方树修改**。
3. **Sonnet（调查 C）**：核对外部 Rockchip SDK 的 UART5_IRQn、INTMUX route、HPMCU IRQ 编号和 RT-Thread 基线。

## 已返回的 Haiku 调查结论

1. NSH banner 输出后进入 `readline_fd` → `read(fd0)` → `uart_read`；接收缓冲区为空时阻塞在 `recvsem`。
2. `uart_register` 只注册 inode；第一次 `uart_open` 才执行 `attach` 和 `rxint(true)`。console 因 `isconsole` 跳过 `setup`。
3. banner 正常而输入无反应最符合以下情况：`attach` 表面成功，但硬件 IRQ / INTMUX 未触发，导致 `uart_read` 永远阻塞。
4. 团队 `chip/rv1126b_idle.c` 已有 `up_idle` polling：UART5 RFNE 时调用 `uart_recvchars`。下一步必须确认该文件是否编入最终 ELF，以及最终 `up_idle` 符号是否来自团队实现。
5. `/dev/console` 与 `/dev/ttyS0` 共享同一个 `uart_dev` 双注册通常可用，不是首要根因。
6. 恢复后优先使用 `nm` / `objdump` 验证 `up_idle` 符号，再结合另外两个 Sonnet 调查核对 INTMUX。

### Sonnet SDK 路由调查已完成

- SDK `UART5_IRQn = 61`，团队 `CONSOLE_UART_IRQ = 61` 正确。
- UART5 对应 INTMUX group 1 bit 29；enable 寄存器地址为 `0x20b40004`，status 寄存器地址为 `0x20b40084`。
- 团队 INTMUX base/layout、`up_enable_irq`、status scan、IPIC SOI/EOI、`riscv_doirq(61)` 调度均与 SDK 一致，未发现 IRQ 编号或路由偏移错误。
- SDK UART5 console 同样是 interrupt-driven，不是 polling。
- 因此恢复后不要优先修改 IRQ 61 / INTMUX 常量；应优先检查 UART IER / IIR / USR 寄存器宏、IRQ 是否实际进入 handler、`up_idle` polling 是否链接生效，并使用调试字符和寄存器值定位。

### Sonnet 团队 UART RX 驱动与调试字符调查已完成

- 调试字符已映射：`A/B/C/D` 为启动阶段；`P` 为 clockconfig；`E/F/G` 表示运行到 `nx_start`；`T/t` 为 timer init；`S/s/0` 为 console / tty 注册；`e0b` 是 M-mode ecall；`L/b/c/l` 为 late init；`!!!` 是前三个 timer tick。系统已完整进入 NSH read 等待。
- Sonnet 审查认为 UART `attach` / `rxint` 流程表面正确。IIR handler 当前只处理一个 pending reason，未来可改为循环处理，但这通常只会导致偶发丢字符，不足以解释完全无输入。
- 该调查提出 NuttX 外部 IRQ 应增加 `RISCV_IRQ_MEXT` offset，但它同时确认团队当前 `attach(61)` 与 `dispatch(61)` 在内部是一致的。

### 冲突审查要求

- 另一份 SDK 专项调查已经通过 `soc.h`、RT-Thread INTMUX 和团队 irq dispatch 证明 raw IRQ `61`、group 1 bit 29、`0x20b40004` / `0x20b40084` 全链一致。
- **不要未经实板证据就把 IRQ 改成 `RISCV_IRQ_MEXT + 61`**；这可能反而破坏当前硬件 enable。恢复后不得直接照第三份调查的 IRQ offset 建议修改。
- 恢复后主模型应先让子代理做以下非侵入验证：
  1. 用 `nm` / `objdump` 确认 `up_idle` 的链接来源。
  2. 在团队 overlay 增加最小调试标记，确认 `uart_attach`、`rxint(true)` 和 ISR entry 是否执行。
  3. 读取并输出 IER、IIR、USR、INTMUX enable / status 寄存器值。
  4. 确认按键时 `UART_USR.RFNE` 是否置位。
  5. 根据上述实板证据再决定最小修复，不得先改 IRQ offset 常量。
- 三份调查现在均已完成。任务 #1 的调查阶段完成，但根因尚未经过实板证实。

## 任务列表

用 `TaskList` 查看是否保留了以下任务（若丢失则重建）：

1. 定位 RV1126B UART RX 失效根因（调查阶段已完成；根因尚未实板证实）
2. 设计团队 overlay 最小修复（等待非侵入实板证据）
3. 实现并编译打包修复
4. 验证实板串口交互

## 恢复后必须执行的顺序

1. **先读取本恢复文档**，以及 memory 中以下条目：
   - `delegate-routine-work-to-subagents.md`
   - `use-codegraph-first.md`
   - `do-not-modify-nuttx-official-tree.md`
   - `rv1126b-openvela-contest-context.md`
2. 用 **TaskList** 查看任务状态。三份调查结果均已记录在本恢复文档中，不要重复做宽泛扫描；由子代理执行针对性的非侵入验证。
3. 主模型**审查三份调查结果及其冲突**，明确“已证实”“推测”和“待实板验证”。不得未经证据采用 `RISCV_IRQ_MEXT + 61` 建议。
4. 优先执行以下验证：
   - 用 `nm` / `objdump` 确认最终 ELF 中 `up_idle` 的符号来源，判断团队 polling 实现是否链接生效。
   - 用团队 overlay 内的最小调试标记确认 `uart_attach`、`rxint(true)` 和 UART ISR entry。
   - 读取并输出 UART IER / IIR / USR 与 INTMUX enable / status 寄存器值。
   - 确认按键时 `UART_USR.RFNE` 是否置位，以区分 UART 硬件接收、polling 和 IRQ handler 链路。
   - 保留已证实结论：raw IRQ 61、INTMUX group 1 bit 29 及当前 route/layout 与 SDK 一致；除非新实板证据推翻，不修改 IRQ 61 / INTMUX 常量。
5. 形成**只修改团队 overlay 的最小方案**。若需修改，实现交给 Sonnet；主模型只审查 diff。
6. 修改后由子代理执行 distclean、编译、objcopy、amp.img、update.img 全流程；主模型审查结果。
7. 让用户烧录测试，并要求提供完整串口日志。**验收标准**：出现 `nsh>` 提示符，回车能刷新提示符，`help` 和 `uname -a` 可执行。

## 推荐的修复策略优先级

- **首选**：先做 `up_idle` 链接来源、attach/rxint/ISR entry 和 UART/INTMUX 寄存器的非侵入验证，再根据实板证据修复团队 overlay。
- **不要优先修改** IRQ 61 / INTMUX 常量，也不得未经证据改成 `RISCV_IRQ_MEXT + 61`；SDK 专项调查已证明当前 raw IRQ 61 路由链一致。
- **备选**：若确认硬件外部 IRQ 尚未实际触发，且团队 `up_idle` polling 未生效，则在团队 overlay 中实现或修正一个最小、安全的 polling RX 临时方案以先打通 NSH；**不得修改官方 NuttX**。
- 保留必要的单字符调试标记和寄存器值输出，验证后再清理；不要一次做无关重构。

---

现在从 TaskList 和三个调查 Agent 的结果开始恢复；主模型仅规划和审查，所有搜索、修改、编译、打包、验证都交给 Sonnet/Haiku。不要修改 nuttx 官方代码。
