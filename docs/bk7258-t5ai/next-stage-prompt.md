# BK7258 T5-AI 下一阶段恢复提示词（N2 已板端验证 → N3）

将下方 fenced text 块完整复制到新会话的第一条消息中，用于继续 BK7258 NuttX 适配工作。
当前基线：**Stage N2 已板端验证（交互式 NSH 可用，2026-07-18）**。

```text
我们正在 openvela 2026 竞赛工作区继续 BK7258 T5-AI NuttX 适配工作。

═══════════════════════════════════════════════════════════════
当前基线（已板端验证，2026-07-18）
═══════════════════════════════════════════════════════════════

Stage N2 完成：NuttX 完整启动到交互式 NuttShell。

  UART1 (460800 8N1):  bootloader banner -> JMP -> N2 DBESITtC ->
                        NuttShell (NSH) -> nsh>
  nsh> help            （列出全部内建命令）
  nsh> uname -a        -> NuttX 0.0.0 ... arm bk7258_t5ai
  nsh> echo hello      -> hello
  键盘输入 + 回显 + Enter + 命令解析全部 live。

UART1 RX 中断驱动、TX 轮询。4 个叠加 RX bug（fifo_port 取位 / rx_enable 未开 /
三道中断门未开 / RX FIFO 阈值=0 → storm）全在 chip/bk7258_serial.c，已全修。
UART1 = NuttX IRQ 31（NVIC 线 15，向量 slot 31 = exception_direct）。

  分支:  contest2026-multi-board
  HEAD:  9f45bc6  feat(bk7258): NuttX Stage N2 — NSH interactive console (board-verified)
  前序:  40495ca (N1) / 088bf72 (docs) / ceead19 (probe+Tier-1 bl) / 783e049 (bl 逆向)

详细 worklog: $CONTEST/docs/bk7258-t5ai/nuttx-port/n2-nsh-console.md
评委主报告:   $CONTEST/docs/bk7258-t5ai/porting-report.md

═══════════════════════════════════════════════════════════════
路径约定（docs 与对话里一律用占位符，不写本机绝对路径）
═══════════════════════════════════════════════════════════════

  export WORKSPACE=/home/lijian/project/open-vela      # openvela 工作区根
  export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"  # 团队 overlay
  export FW="$WORKSPACE/nuttx"                          # 构建产物目录（all-app.bin 等）
  export BK7258_SDK=/home/lijian/project/armino/bk_avdk_smp
  export ZEPHYR_PORT=/home/lijian/project/TuyaOpen/zephyr-bk7258-port

规则：
- 持久路径（docs / commit message / 提示词）只用 $WORKSPACE / $CONTEST / $FW 等占位符。
- 环境变量未设置时先询问用户。
- BK7258 与 RV1126B 是两块独立板子，文档隔离在 docs/bk7258-t5ai/ 和 docs/rv1126b-hpmcu/。

═══════════════════════════════════════════════════════════════
改动边界（硬约束）
═══════════════════════════════════════════════════════════════

- 所有改动只在团队 overlay $CONTEST 里（board/bk7258_t5ai/、docs/bk7258-t5ai/）。
  对应 linkfile 映射: $CONTEST/board/bk7258_t5ai -> vendor/openvela/boards/contest2026_135_bk7258
- 不修改 nuttx/apps/packages/vendor 外层官方 checkout（除非另开上游 PR）。
- 不 push、不 flash、不主动跑构建，除非用户明确授权。

═══════════════════════════════════════════════════════════════
工作方式（恢复规则）
═══════════════════════════════════════════════════════════════

- CodeGraph first：本仓已建 .codegraph/ 索引。定位/理解代码先查 codegraph_explore
  (projectPath=$WORKSPACE)，再考虑 grep/find/读文件。
- main 模型只做规划 / 苏格拉底式澄清 / 证据审核 / 委派；搜索、机械编辑、重复验证、
  实现批量委派普通 subagent 做。
- 授权门禁：build / menuconfig / 打包 / 刷机 / commit / push 均需用户明确授权。
- 已验证 vs 未验证：只有板端观察才算已验证；构建通过 ≠ 板端成功。
- 阶段化去风险：每一步在板端验证，保留已知良好镜像作回退。
- 不主动加载 skill、不使用 Workflow（除非用户明确要求）。

═══════════════════════════════════════════════════════════════
已确认的芯片/启动事实（N1/N2 沿用，无需重证）
═══════════════════════════════════════════════════════════════

BK7258 / 涂鸦 T5-AI：
  - CPU: ARM Cortex-M33 三核（CPU0=boot master，CPU1+CPU2=AP 副核）
  - 启动链: BootROM → bootloader(0x02000000) → app(0x02010000)
  - SRAM 640KB (0x28000000-0x280A0000)，SP 顶 = 0x2809FFFC
  - Flash 物理: 32B数据+2B CRC16 (poly 0x8005, BE)，flash 控制器硬件透明解码
  - app 烧 physical 0x11000（= logical 0x02010000）
  - app magic "BK7236\0\0" @ 文件偏移 0x100（向量表槽 64/65）

UART1 关键寄存器（已板端验证）：
  - CFG          0x45830010  (bit0=tx_enable, bit1=rx_enable; clk_div=0x37 -> 460800)
  - fifo_config  0x45830014  (bits[8:15]=RX FIFO 阈值，必须设 ≥1，否则 storm)
  - fifo_status  0x45830018  (bit20=TX FIFO not full)
  - fifo_port    0x4583001C  (bits[0:7]=TX, bits[8:15]=RX)
  - int_status   0x4583001C 区附近（见 bk7258_serial.c）
  - int_enable   0x45830020  (bit1=RX int enable)
  - ICU SYS_CPU0_INT_0_31_EN 0x44010080  (bit15=UART1 → CPU0 线15)
  - NVIC IRQ 31 = UART1（ICU_PRI_IRQ_UART1=26 是优先级索引，非 IRQ 号，别混）

═══════════════════════════════════════════════════════════════
下一阶段（Stage N3）候选方向
═══════════════════════════════════════════════════════════════

基线 N2 = NSH 可交互。N3 候选（按由易到难 / 价值排序，先与用户确认选哪个）：

1. procfs + ps：在 NSH 里能 `ps` / `cat /proc/*`，观测任务/调度。开 CONFIG_FS_PROCFS，
   接 NuttX 标准 procfs，无新硬件依赖，最低风险。
2. MTD + 文件系统：接 flash MTD（层叠在硬件透明 CRC 之上做逻辑块访问），挂 LittleFS 或
   SmartFS，让 `ls`/`cat`/`mount` 可用。需确认 BK7258 flash 控制器写时序。
3. Tier-2 bootloader（OTA）：RBL 头校验 + A-B 分区 + failover。参考 BK 官方 §2.12 RBL 校验
   （逆向文档在 docs/bk7258-t5ai/bootloader/）。需 flash 写。
4. 多核 SMP：CPU1/CPU2 唤醒（app 层 start_cpu1_core，见 SDK system_main.c）+ NuttX SMP。
   参考 RP2040 / CXD56xx。最复杂，建议放最后。

验收标准（每个候选都先与用户敲定）：
- procfs/ps：`ps` 列出 NuttX 任务；`cat /proc/*/status` 有内容。
- MTD/FS：`mount` 显示挂载点，`ls` 列文件，掉电后文件还在。
- Tier-2 OTA：A 槽跑、烧 B 槽、重启切 B、B 损坏回退 A。
- SMP：CPU1 跑 NuttX 任务，`ps` 显示双核任务。

═══════════════════════════════════════════════════════════════
开局动作
═══════════════════════════════════════════════════════════════

1. cd $CONTEST && git status / git log --oneline -5 确认工作树状态。
2. 问用户：N3 先做哪个候选（默认建议从 procfs/ps 起，风险最低）。
3. 选定后先出阶段计划（目标/范围/验收/任务拆解），用户确认后再动手。
```

## 当前状态

- 分支: `contest2026-multi-board`
- HEAD: `9f45bc6`（feat: Stage N2 — NSH interactive console，board-verified）
- 已验证产物: `$FW/all-app.bin`（= `bl_crc.bin` + `nuttx_crc.bin`），整体烧 @ physical `0x0`
- 下一步: 见上方 N3 候选，优先 `procfs` / `ps`
