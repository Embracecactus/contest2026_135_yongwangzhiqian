# BK7258 T5-AI 下一阶段恢复提示词

将下方 fenced text 块完整复制到新会话的第一条消息中，用于继续 BK7258 NuttX 适配工作。

```text
我们正在 openvela 2026 竞赛工作区继续 BK7258 T5-AI NuttX 适配工作。

当前阶段：技术探索已完成 bootloader 逆向，准备进入 NuttX BSP 移植。

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
  - Flash 镜像格式: 32字节数据 + 2字节CRC16（物理 34 字节）
    CRC 由 flash 控制器硬件透明处理，CPU 看逻辑地址，bootloader 不需软件解码
  - SRAM: 640KB (0x28000000-0x280A0000)，SP 顶 = 0x2809FFFC
  - PSRAM: 8MB (0x60000000)
  - 启动格式兼容标识: "BK7236"（magic: bootloader 用 BK7236\x10\x00，app 用 BK7236\0\0）

涂鸦 vs BK 官方 bootloader（已逆向对比）：
  - 涂鸦: 65KB，magic 在 0x110，有分区表指针和额外向量
  - BK 官方: 52KB，magic 在 0x100，标准结构
  - 两者代码核心一致，唯一实质差异是 magic 偏移

═══════════════════════════════════════════════════════════════
bootloader 结论（关键技术判断）
═══════════════════════════════════════════════════════════════

1. 已有最小 bootloader（$ZEPHYR_PORT/bootloader/bk7236_min_bl.S）功能完整：
   header 检查(MSP/Reset/Magic) → VTOR → MSP → bx 跳转。Zephyr 已验证可跳转。
2. bootloader 不需要处理 CRC——CRC 是 flash 控制器硬件的事。
3. NuttX bin 只需编译后做 CRC 扩展打包（构建环节），bootloader 直接按逻辑地址跳转。
4. 自定义 bootloader 方案确定：复用最小 bootloader，可选支持 magic 偏移可配置（0x100/0x110）。

═══════════════════════════════════════════════════════════════
资源
═══════════════════════════════════════════════════════════════

- BK7258 SDK (ARMINO): $BK7258_SDK（ap/ + cp/，已用 hardware-context 索引）
- Zephyr port（含 bootloader + 打包脚本 + SoC 代码）: $ZEPHYR_PORT
- 已有逆向文档: $ZEPHYR_PORT/docs/12-custom-bootloader.md, 11-bootrom-analysis.md
- NuttX SMP 可行性: 已分析（Cortex-M33 SMP 需平台特定 up_cpu_start，参考 RP2040/CXD56xx）

═══════════════════════════════════════════════════════════════
下一阶段目标
═══════════════════════════════════════════════════════════════

实现 BK7258 NuttX BSP 最小 NSH baseline（单核 CPU1 先行）：

1. 创建 NuttX board 目录（参考 RV1126B 的 board/contest_board 结构 + BK7236N openvela 适配）
2. 链接脚本: app 基址 0x02010000，SP 0x2809FFFC，Magic "BK7236\0\0" @0x100
3. 启动代码: Cortex-M33 vector table + Reset_Handler（复用 SDK startup_cpu0.c 模式）
4. 基础驱动: UART（参考 bootloader 的 UART1 初始化）、clock、NVIC
5. 打包: nuttx.bin → CRC 扩展（复用 $ZEPHYR_PORT/tools/bk7258_crc_expand_app.py）→ 烧录

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

## 当前 Git 状态

- 分支: submit-rv1126b-nsh-baseline
- HEAD: 8a6b83e（含 Skills 迭代 + BK7258 分析）
- BK7258 工作产物: `docs/bk7258-t5ai/`（README + bootloader 对比 + 本恢复提示词）
