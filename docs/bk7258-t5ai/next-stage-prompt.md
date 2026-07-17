# BK7258 T5-AI 下一阶段恢复提示词（bootloader 逆向已完成）

将下方 fenced text 块完整复制到新会话的第一条消息中，用于继续 BK7258 NuttX 适配工作。

```text
我们正在 openvela 2026 竞赛工作区继续 BK7258 T5-AI NuttX 适配工作。

当前阶段：bootloader 完整逆向已完成，准备进入 NuttX BSP 移植。

═══════════════════════════════════════════════════════════════
路径约定
═══════════════════════════════════════════════════════════════

  export WORKSPACE=/home/lijian/project/open-vela
  export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
  export BK7258_SDK=/home/lijian/project/armino/bk_avdk_smp
  export ZEPHYR_PORT=/home/lijian/project/TuyaOpen/zephyr-bk7258-port

规则：
- 持久路径只使用变量，不写个人绝对路径。
- 环境变量未设置时先询问用户。
- BK7258 与 RV1126B 是两块独立板子，文档隔离在 docs/bk7258-t5ai/ 和 docs/rv1126b-hpmcu/。
- 当前 git 分支: contest2026-multi-board（多板共用，非 rv1126b 专用）。
- 不主动加载 skill，除非用户明确要求。
- 不使用 Workflow。
- 主模型只做规划/审核；委托普通 Agent 做搜索/实现/验证。

═══════════════════════════════════════════════════════════════
芯片事实（已确认）
═══════════════════════════════════════════════════════════════

BK7258 / 涂鸦 T5-AI：
  - CPU: ARM Cortex-M33，三核（CPU0=CP 通信核，CPU1+CPU2=AP 应用核 SMP）
  - 无线: Wi-Fi 6 + BLE 5.4
  - 启动链: BootROM → bootloader(0x02000000) → app(0x02010000)
  - SRAM: 640KB (0x28000000-0x280A0000)，SP 顶 = 0x2809FFFC
  - PSRAM: 8MB (0x60000000)

Flash 镜像格式：
  - 物理: 32字节数据 + 2字节CRC16（多项式 0x8005，big-endian）
  - CRC 由 flash 控制器硬件透明处理，CPU 看逻辑地址
  - 转换: physical = (logical / 32) * 34 + (logical % 32)
  - bootloader 不需软件 CRC 解码
  - app 烧录物理偏移 0x11000（对应逻辑 0x02010000）

启动格式标识：
  - bootloader magic: "BK7236\x10\x00" @ logical 0x100
  - app magic: "BK7236\0\0" @ app offset 0x100

═══════════════════════════════════════════════════════════════
bootloader 逆向结论（完整，见 docs/bk7258-t5ai/bootloader/）
═══════════════════════════════════════════════════════════════

涂鸦(65KB) vs BK官方(52KB) 已完整逆向：
  - 两者核心启动逻辑一致，涂鸦额外加了 OTA(diff2ya) + FAL 分区表
  - boot magic 位置一致（logical 0x100）
  - CRC 都是 flash 控制器硬件处理
  - 多核唤醒都不是 bootloader 责任（由 app 层 start_cpu1_core 负责）

app 加载主路径（两者一致）：
  1. 读 app 向量表 @ 0x02010000: MSP 范围校验 + Reset Thumb 位校验
  2. 校验 app magic @ 0x02010100: "BK7236\0\0"
  3. 设 VTOR = 0x02010000
  4. 设 MSP = app MSP
  5. 清寄存器 r0-r12, DSB, ISB
  6. bx app Reset_Handler

已有最小 bootloader（$ZEPHYR_PORT/bootloader/bk7236_min_bl.S）功能完整，
Zephyr 已验证可跳转。无需修改 bootloader 逻辑。

关键寄存器（逆向确认）：
  - WDT: AON 0x44000600, APB 0x44800008/10, key 0x5A/0xA5
  - UART1: 0x45830008/10/1C
  - GPIO: 0x440100C0/C4, 0x44000400+
  - SWD: 0x440100E0
  - SCB VTOR: 0xE000ED08
  - Flash 控制器: 0x44000000, SYS_REG: 0x44010000

═══════════════════════════════════════════════════════════════
资源
═══════════════════════════════════════════════════════════════

- BK7258 SDK (ARMINO): $BK7258_SDK（已索引，见 docs/bk7258-t5ai/sdk-context-index.md）
- Zephyr port（含 bootloader + 打包脚本 + SoC 代码）: $ZEPHYR_PORT
- 逆向文档: docs/bk7258-t5ai/bootloader/
  - full-reverse-synthesis.md（综合结论）
  - tuya-bootloader-reverse.md（涂鸦 761 行）
  - bk-official-bootloader-reverse.md（BK 官方 940 行）
  - vendor-bootloader-comparison.md（初步对比）
- 打包脚本: $ZEPHYR_PORT/tools/bk7258_crc_expand_app.py
- NuttX SMP 参考: RP2040（Cortex-M0+ SMP）、CXD56xx（多核）

═══════════════════════════════════════════════════════════════
下一阶段目标
═══════════════════════════════════════════════════════════════

实现 BK7258 NuttX BSP 最小 NSH baseline（单核 CPU1 先行）：

1. 创建 NuttX board 目录（参考 RV1126B board/contest_board 结构）
2. 链接脚本: app 基址 0x02010000，SP 0x2809FFFC，Magic "BK7236\0\0" @0x100
3. 启动代码: Cortex-M33 vector table + Reset_Handler（参考 SDK startup_cpu0.c）
4. 基础驱动: UART（UART1 @ 0x45830000，参考 bootloader 初始化）、clock、NVIC
5. 打包: nuttx.bin → bk7258_crc_expand_app.py → 烧录 physical 0x11000

验收标准: bootloader 跳转后 NuttX NSH 能在 UART1 打印提示符。

═══════════════════════════════════════════════════════════════
严格交互规则
═══════════════════════════════════════════════════════════════

- 苏格拉底式澄清：范围/意图不清先问，不自行扩展。
- 阶段规划：每阶段明确 目标/范围/验收标准/任务拆解。
- 授权门禁：构建/打包/刷机/PR 需用户明确授权。
- 已验证 vs 未验证：只有板端观察才算已验证；构建通过 ≠ 板端成功。
- 不修改外层 nuttx/apps 官方 checkout（除非做上游 PR）。
```

## 当前状态

- 分支: contest2026-multi-board
- HEAD: 53be0a7（bootloader 逆向尚未提交，工作树有新增逆向文档）
- BK7258 工作产物: docs/bk7258-t5ai/bootloader/（4 份逆向文档 + 综合）
