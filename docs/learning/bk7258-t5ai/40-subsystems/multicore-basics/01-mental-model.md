# 多核基础：BK7258 三核架构与 CP/AP 分离

本篇讲解 BK7258 芯片的多核架构、CPU0（CP）与 CPU1（AP）的独立 NuttX 镜像模型、启动链、mailbox doorbell 通信协议和共享内存 boot state 设计。

> **来源记录**
>
> - 教学主题：BK7258 三核架构、CP/AP 分离与 CPU1 独立 NuttX 启动
> - `$CONTEST` source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 实现 source：
>   - `$BOARD/chip/include/bk7258_amp.h` — AMP 布局、mailbox、boot state
>   - `$BOARD/chip/bk7258_ap_control.c` — CPU0 侧控制逻辑
>   - `$BOARD/chip/bk7258_ap_start.c` — CPU1 复位入口
>   - `$BOARD/chip/bk7258_ap_main.c` — CPU1 NuttX 初始化
>   - `$BOARD/chip/bk7258_ap_vectors.c` — CPU1 专用向量表
> - 最后核对日期：2026-07-27
> - 验证状态：Stage N7 build-verified（CPU0 `apctl` 命令、CPU1 独立单核镜像编译通过），未板测
> - 教学简化：本文不展开 SMP（对称多处理）与 AMP（非对称多处理）的完整理论对比；不覆盖锁调度、cache coherency 细节

## 1. BK7258 三核架构概览

BK7258 芯片包含三个处理器核心：

```text
┌──────────────────────────────────────────────────┐
│                   BK7258 SoC                       │
│                                                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────────┐    │
│  │  CPU0    │  │  CPU1    │  │  Wi-Fi/BT     │    │
│  │ CM33     │  │ CM33     │  │  Co-processor │    │
│  │ (CP)     │  │ (AP)     │  │  (closed)     │    │
│  └────┬─────┘  └────┬─────┘  └──────────────┘    │
│       │             │                               │
│  ┌────┴─────────────┴─────┐                        │
│  │     共享 SRAM/Flash     │                        │
│  └─────────────────────────┘                        │
└──────────────────────────────────────────────────┘
```

| 核心 | 标识 | 角色 | NuttX 镜像 | 当前状态 |
|---|---|---|---|---|
| CPU0 | CP (Control Processor) | 系统控制、外设驱动、NSH | `nuttx.bin`（主镜像） | board-verified |
| CPU1 | AP (Application Processor) | 独立计算任务 | `nuttx_ap.bin`（独立镜像） | build-verified |
| Wi-Fi/BT | 协处理器 | 无线协议栈 | 闭源固件 | 不涉及移植 |

## 2. 两种多核模型：SMP vs AMP

### SMP（对称多处理）

```text
┌────────────────────────────┐
│     一个 NuttX 内核        │
│  在 CPU0 和 CPU1 上各运行  │
│  一个线程调度器            │
│  共享所有内核数据结构      │
└────────────────────────────┘
```

- CPU0 和 CPU1 共享同一个 `.text`、`.data`、堆和全局变量
- 一个线程可以在 CPU0 开始执行，被调度到 CPU1 继续
- 需要 spinlock 保护所有共享数据结构

### AMP（非对称多处理）— BK7258 采用的方式

```text
┌──────────────┐   ┌──────────────┐
│  CPU0 NuttX  │   │  CPU1 NuttX  │
│  (CP 镜像)   │   │  (AP 镜像)   │
│              │   │              │
│  独立堆/栈   │   │  独立堆/栈   │
│  独立向量表  │   │  独立向量表  │
│  独立外设    │   │  独立外设    │
└──────┬───────┘   └──────┬───────┘
       │                  │
       └──── mailbox ─────┘
          共享 boot state
```

- CPU0 和 CPU1 分别运行**不同的 NuttX 镜像**
- 各自拥有独立的栈、堆、向量表、调度器
- 通过 shared memory（mailbox + boot state）通信
- CPU0 控制 CPU1 的启动、停止、重启

## 3. Flash 和 SRAM 的物理划分

BK7258 有 4MB Flash（XIP at `0x02000000`）和 640KB SRAM（`0x28000000`）：

### Flash 布局

```text
0x02000000 ┌─────────────────┐
           │  Bootloader      │  64 KB
0x02010000 ├─────────────────┤
           │  CP NuttX (CPU0) │  960 KB
0x02100000 ├─────────────────┤
           │  LittleFS Data   │  1024 KB
0x02200000 ├─────────────────┤
           │  AP NuttX (CPU1) │  2048 KB
0x02400000 └─────────────────┘
```

### SRAM 布局

```text
0x28000000 ┌─────────────────┐
           │  CP RAM (CPU0)  │  320 KB
0x28050000 ├─────────────────┤
           │  AP RAM (CPU1)  │  316 KB
0x2809f000 ├─────────────────┤
           │  Shared Page    │  4 KB  ← boot state 所在地
0x280a0000 └─────────────────┘
```

关键设计点：
- Shared Page（`BK7258_SHARED_RAM_BASE`）是两核都能访问的 4KB 内存页
- `struct bk7258_ap_boot_state_s` 放在 Shared Page 的起始位置
- CP RAM 和 AP RAM 互不重叠，避免了 AMP 中最常见的 cache line 冲突

## 4. CPU1 启动链

CPU0 启动 CPU1 的完整流程：

### 第一步：准备 boot state（CPU0）

```text
CPU0 通过 bk7258_ap_start()
  → bk7258_ap_state_prepare()：
    - 置 magic = "APBS" (0x53425041)
    - 置 version = 1
    - 置 command = BK7258_AP_COMMAND_START
    - 置 state = BK7258_AP_STATE_STARTING
```

### 第二步：配置硬件（CPU0）

```text
bk7258_ap_start_locked()
  → sys_drv_set_cpu1_pwr_dw(0)           // 上电 CPU1
  → sys_drv_set_cpu1_boot_address_offset() // 设置 CPU1 启动地址
  → sys_drv_set_cpu1_reset(1)             // 释放 CPU1 复位
  → bk7258_ap_wait(STATE_READY, timeout) // 等待 CPU1 就绪
```

这些 `sys_drv_*` 函数来自 BK7258 CP SDK 的预编译 archive。它们操作的是 SYS 寄存器组（`0x44010000` 区域），控制 CPU1 的电源、复位和启动地址。

### 第三步：CPU1 独立启动（CPU1）

```text
bk7258_ap_start.c: __start()
  → 关全局中断 (cpsid i)
  → 设置 local_core_id = 0, physical_core_id = 1
  → VTOR = BK7258_AP_FLASH_ADDR（CPU1 专用向量表）
  → 复制 .data 段、清零 .bss
  → 使能 FPU
  → bk7258_ap_main()
```

### 第四步：AP NuttX 初始化（CPU1）

```text
bk7258_ap_main()
  → 验证 VTOR / core ID / heap
  → 设置 boot state: state = READY
  → 发送 mailbox READY 事件给 CPU0
  → 进入主循环：轮询 mailbox、维护 heartbeat
```

### 第五步：CPU0 确认就绪

```text
bk7258_ap_wait() 收到 STATE_READY
  → apctl start 返回成功
  → CPU1 独立运行
```

## 5. CPU0-CPU1 通信协议

### Mailbox 硬件

BK7258 有两组硬件 mailbox：

| Mailbox | 方向 | 基地址 |
|---|---|---|
| MBOX0 | CPU0 → CPU1 | `0x41000000` |
| MBOX1 | CPU1 → CPU0 | `0x41020000` |

每个 mailbox 有六个 32 位寄存器窗口：CLKRST、READY、CLEAR、SENDER、RECEIVER、PARAM0-3。

### Doorbell 协议

```text
发送方                             接收方
───────                            ───────
写 PARAM0 = DOORBELL_MAGIC         检查 READY bit
写 PARAM1 = event                  验证 PARAM0 = DOORBELL_MAGIC
写 PARAM2 = generation             验证 PARAM2 = generation
写 PARAM3 = state                  读取 event = PARAM1
dmb sy                             ACK（写 CLEAR，清 READY）
写 READY = BOX0_BIT
dsb sy; sev
```

### 事件类型

| 事件 | 方向 | 含义 |
|---|---|---|
| `EVENT_READY` | AP → CP | AP NuttX 已初始化并就绪 |
| `EVENT_STOP` | CP → AP | CP 请求 AP 停止 |
| `EVENT_STOPPED` | AP → CP | AP 已安全停止 |
| `EVENT_FAILED` | AP → CP | AP 启动或运行时错误 |

### boot state 共享内存

```c
struct bk7258_ap_boot_state_s  // 位于 BK7258_SHARED_RAM_BASE
{
  magic, version, size, generation;   // 协议版本和代数
  command, state, error, last_event;  // 控制面
  local_core_id, physical_core_id;    // 核心身份
  initial_vtor, initial_msp;          // 启动参数
  runtime_vtor, runtime_msp;          // 运行时验证
  clock_hz, systick_*;               // 时钟自检
  heap_start, heap_end, heap_test;    // 堆验证
  cp_to_ap_doorbells;                 // CP→AP doorbell 计数
  ap_to_cp_doorbells;                 // AP→CP doorbell 计数
  heartbeat;                          // AP 心跳
  ram_start, ram_end;                 // SRAM 边界
  flash_start, flash_end;             // Flash 边界
};
```

boot state 的价值：
- CPU0 写入 `command` 后 CPU1 轮询判断需要做什么
- CPU1 写入 `state`/`error` 后 CPU0 轮询判断是否成功
- `generation` 防止跨重启的状态混淆
- `heartbeat` 让 CPU0 监控 AP 是否存活

## 6. AP 状态机

```text
                    ┌──────────┐
        apctl start │  STATE   │ apctl stop
        ──────────→ │  OFF     │ ←──────────
                    └────┬─────┘
                         │ CPU0 准备 boot state
                         ▼
                    ┌──────────┐
                    │ STARTING │
                    └────┬─────┘
                         │ CPU1 初始化完成
                    ┌────┴─────┐
               ┌───→│  READY   │←───┐
               │    └────┬─────┘    │
               │         │          │
          apctl restart  │    apctl stop
               │         ▼          │
               │    ┌──────────┐    │
               └────│ STOPPING │────┘
                    └────┬─────┘
                         │ CPU1 ack
                    ┌────┴─────┐
                    │ STOPPED  │
                    └──────────┘
                          │ 可再次 start
                          ▼
                    ┌──────────┐
                    │ STARTING │ ...
                    └──────────┘
```

任何阶段都可能进入 `FAILED` 状态（启动超时、堆错误、VTOR 不正确等）。

## 7. CPU1 专用向量表

CPU1 拥有自己的向量表，位于 `bk7258_ap_vectors.c`：

```text
CPU0 向量表（Flash @ 0x02010000）  vs  CPU1 向量表（Flash @ 0x02200000）
  slot[0]  MSP                        slot[0]  MSP（独立值）
  slot[1]  __start (CP 入口)          slot[1]  __start (AP 入口)
  slot[2]  NMI                        slot[2]  NMI
  ...                                  ...
  slot[15] SysTick                    slot[15] SysTick
  slot[16] IRQ0                       slot[16] IRQ0
  ...                                  ...
```

两个向量表是完全独立的：
- CPU0 的 VTOR 永远指向 CP 镜像地址
- CPU1 的 VTOR 在 `__start` 中设置为 AP 镜像地址
- 两个核心各自的中断处理互不干扰

## 8. 自测题

1. BK7258 的三个核心分别是什么？CP 和 AP 分别指什么？
2. SMP 和 AMP 的关键区别是什么？BK7258 用的是哪种？
3. CPU0 启动 CPU1 需要哪几步？`sys_drv_set_cpu1_boot_address_offset()` 的作用是什么？
4. Mailbox doorbell 协议中 `PARAM2 = generation` 的作用是什么？
5. 为什么 CP RAM 和 AP RAM 需要物理上不重叠？
6. boot state 中的 `heartbeat` 字段是谁更新、谁读取的？

答案：

1. CPU0（CP，Control Processor）、CPU1（AP，Application Processor）、Wi-Fi/BT 协处理器。CP 是系统主控，AP 是独立计算单元。
2. SMP 中多个核心共享同一个内核镜像和调度器；AMP 中每个核心运行独立的内核镜像。BK7258 使用 AMP。
3. 准备 boot state → 上电 CPU1 → 设置启动地址 → 释放复位 → 等待就绪。`sys_drv_set_cpu1_boot_address_offset()` 告诉硬件 CPU1 从 Flash 的哪个偏移量加载第一条指令。
4. 防止跨重启的状态混淆——boot state 可能保留了上一次 AP 运行的旧值，generation 确保接收方不会把旧消息当作当前消息。
5. 在 AMP 中两个核心没有 cache coherency 协议，如果物理内存重叠，同一个地址可能被两个核心的 cache 分别缓存不同的值。物理不重叠意味着每个地址只有一个所有者。
6. CPU1（AP）更新 heartbeat，CPU0（CP）通过 `bk7258_ap_get_status()` 读取它来监控 AP 是否存活。
