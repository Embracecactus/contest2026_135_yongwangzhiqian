# 涂鸦 T5-AI Bootloader 样本逆向笔记（历史）

> 本文只保存 `t5ai_bootloader.bin` 样本的逆向证据，不定义现行板级命名、启动链、
> 信任链或下载流程。现行入口见 [BK7258 平台文档](../README.md)与
> [构建、烧录与调试 SOP](../nuttx-port/bk7258-build-flash-debug-sop.md)。

## 0. 元信息

| 项 | 值 |
|---|---|
| 二进制文件 | `t5ai_bootloader.bin` (65536 B = 0x10000) |
| 处理器 | Cortex-M33 (ARMv8-M, Thumb-2) |
| 链接基址 | `0x02000000` (flash XIP logical) |
| 反汇编工具 | `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x02000000` |
| SDK 交叉引用 | `startup_cpu0.c`, `system_main.c`, `reg_base.h` |

---

## 1. 历史重建的启动流程

```
BootROM (mask ROM @ 0x06000000)
  |
  |-- 从 flash physical 0x0 读取 bootloader 区域 (CRC-expanded 0x11000)
  |-- 校验 bootloader magic @ logical 0x100: "BK7236\x10\x00"
  |-- 解码 CRC -> logical view -> XIP @ 0x02000000
  |-- 设置 MSP = *(0x02000000) = 0x28030000
  |-- 跳转到 Reset_Handler = *(0x02000004) = 0x020001C1
  v
Reset_Handler @ 0x020001C0
  |
  |-- [Phase 1] 系统初始化
  |   |-- 关闭中断 (CPSID I, 推测)
  |   |-- 初始化 flash 控制器 (0x44030000)
  |   |-- 初始化时钟系统 (SYS_REG 0x44010000)
  |   |-- 关闭/喂看门狗 (AON_WDT 0x44000600, APB_WDT 0x44800000)
  |   |-- 配置 GPIO (0x44000400-0x44000404)
  |   |-- 初始化 UART1 (0x45830000) 用于调试输出
  |   |-- 打印 " u_bootloader enter" @ 0x02007EFA
  |
  |-- [Phase 2] FAL 分区表初始化
  |   |-- 查找 "bootloader" 分区
  |   |-- 查找 "app" / "app1" 分区
  |   |-- 使用 beken_onchip_crc flash 驱动
  |
  |-- [Phase 3] App 加载与校验
  |   |-- 从 app 分区读取 app header @ logical 0x02010000
  |   |-- 检查 MSP: 0x28000000 <= MSP <= 0x280A0000
  |   |-- 检查 Reset_Handler: bit0 == 1 (Thumb)
  |   |-- 检查 Magic @ offset 0x100: "BK7236\0\0"
  |   |-- (可选) 校验 PATCH_HEADER: magic_ver, bin_type, src_crc32
  |
  |-- [Phase 4] 跳转 App
      |-- 打印 "jump to:0x%x\n" @ 0x02007F80
      |-- 设置 VTOR = 0x02010000 (SCB @ 0xE000ED08)
      |-- 设置 MSP = app MSP
      |-- BX app_Reset_Handler
```

---

## 2. 向量表结构 (offset 0x000-0x0FF)

| 偏移 | 索引 | 值 | 含义 |
|------|------|-----|------|
| 0x000 | [0] | `0x28030000` | Initial MSP (SRAM 192KB 处) |
| 0x004 | [1] | `0x020001C1` | Reset_Handler (Thumb) |
| 0x008 | [2] | `0x02000143` | NMI_Handler |
| 0x00C | [3] | `0x02000141` | HardFault_Handler |
| 0x010 | [4] | `0x02000145` | MemManage_Handler |
| 0x014 | [5] | `0x02000145` | BusFault_Handler |
| 0x018 | [6] | `0x02000145` | UsageFault_Handler |
| 0x01C | [7] | `0x02000145` | SecureFault_Handler |

**关键发现**: 所有 fault handler 共享同一入口 `0x02000145`，这是一个简单的死循环 (`b.n .`)。这意味着 bootloader 不处理任何异常，遇到 fault 直接挂死。

### 2.1 异常处理代码

```asm
; HardFault @ 0x02000140
02000140: e7fe    b.n 0x2000140    ; 无限循环

; NMI @ 0x02000142
02000142: e7fe    b.n 0x2000142    ; 无限循环

; MemManage/BusFault/UsageFault/SecureFault @ 0x02000144
02000144: e7fe    b.n 0x2000144    ; 无限循环
```

---

## 3. 分区表结构

### 3.1 分区表指针 (offset 0x100-0x10F)

| 偏移 | 值 | 含义 |
|------|-----|------|
| 0x100 | `0x00000000` | 保留 |
| 0x104 | `0x00000000` | 保留 |
| 0x108 | `0x00000000` | 保留 |
| 0x10C | `0x29800000` | 分区表指针/标志 |

`0x2980` 的含义:
- 十进制 = 10624
- 可能表示 bootloader 代码区的 logical size (10624 bytes)
- 或者是分区表的某种编码格式

### 3.2 Boot Magic (offset 0x110)

```
0x110: 42 4B 37 32 33 36 10 00 = "BK7236" + 0x10 + 0x00
```

这是 BootROM 识别 bootloader 的标识。BootROM 在 flash physical 0x110 (logical 0x100) 处查找此 magic。

### 3.3 FAL 分区表 (offset 0x81C0+)

Bootloader 内嵌了 FAL (Flash Abstraction Layer) 分区表，包含以下分区:

| 分区名 | 偏移 | Flash 设备 | 备注 |
|--------|------|-----------|------|
| bootloader | 0x81C4 | beken_onchip_crc | bootloader 区域 |
| app | 0x8208 | beken_onchip_crc | 主 app 分区 |
| app1 | 0x824C | beken_onchip_crc | 备份 app |
| app2 | 0x8290 | beken_onchip_crc | 第二备份 |
| download | 0x82D4 | beken_onchip_crc | OTA 下载区 |

分区表有两份副本 (0x81C0 和 0x8310 区域)，用于冗余校验。

每个 FAL 分区条目格式:
```
+0x00: magic_ver (1 byte, 通常 0x30)
+0x01: type (1 byte, 通常 0x31)
+0x02: "PE" + name (null-terminated)
+...:  flash_device_name (null-terminated, "beken_onchip_crc")
+...:  offset/size (4 bytes, aligned)
```

### 3.4 App 分区定位

App 分区名 "app" 或 "app1"，使用 `beken_onchip_crc` flash 驱动。Bootloader 通过 FAL 接口:
1. `fal_flash_device_find("beken_onchip_crc")` - 找到 flash 设备
2. `fal_partition_find("app")` - 找到 app 分区
3. 从分区的 `partition_start_addr` 读取 app header

App 在 flash 中的 physical 布局:
```
Physical 0x00000: bootloader (logical 0x10000, physical 0x11000 after CRC expand)
Physical 0x11000: app (logical 0x10000, physical depends on actual size)
Logical  0x02000000: bootloader XIP base
Logical  0x02010000: app XIP base
```

---

## 4. App Header 校验

### 4.1 App Header 格式

```
App base = 0x02010000 (logical XIP)

偏移       | 大小 | 内容
0x000-0x003 | 4B  | MSP (初始栈指针)
0x004-0x007 | 4B  | Reset_Handler (入口地址, bit0=1)
0x008-0x0FF | ... | 扩展向量表 (IRQ handlers)
0x100-0x107 | 8B  | Magic "BK7236\0\0"
0x108-0x13F | ... | 扩展中断向量 (IRQ 66-79)
0x200+      | ... | App 代码
```

### 4.2 校验项

Bootloader 检查以下字段:

1. **MSP 范围**: `0x28000000 <= MSP <= 0x280A0000` (640KB SRAM)
2. **Reset Thumb 位**: `Reset_Handler & 1 == 1`
3. **Magic**: offset 0x100 处必须是 `BK7236\0\0` (bytes: `42 4B 37 32 33 36 00 00`)

### 4.3 Magic 在向量表中的位置

根据 `startup_cpu0.c`:
```c
/* BK7236 legacy download mode requires that the flash offset 0x100 is 'BK7236'.*/
(void (*)(void))0x32374B42,  // = "BK72" in little-endian
(void (*)(void))0x00003633,  // = "36\0\0" in little-endian
```

这两个 32-bit word 占据 IRQ48 和 IRQ49 的位置 (向量表 entry 64/65, offset 0x100/0x104)。

### 4.4 PATCH_HEADER 校验

Bootloader 还可能校验 diff2ya/OTA 的 PATCH_HEADER:
- `magic_ver`: 魔法版本号
- `bin_type`: 二进制类型
- `src_crc32`: 源 CRC32
- `dst_crc32`: 目标 CRC32
- `src_length` / `dst_length`: 源/目标长度

相关字符串 (offset 0x6A4A+):
```
"diff2ya_header.magic_ver = 0x%x"
"diff2ya_header.bin_type = 0x%x"
"diff2ya_header.src_crc32 = 0x%x"
```

---

## 5. CRC 校验

### 5.1 Flash 硬件 CRC

Bootloader 使用 `beken_onchip_crc` flash 驱动，这是 Beken 芯片的 **硬件 CRC 引擎**，不是软件 CRC。

相关字符串:
- 0x7F10: `"beken_onchip"`
- 0x7F48: `"beken_onchip_crc"`
- 0x8035: `"beken_onchip_crc"` (fal_flash_device_find 注册名)

### 5.2 Flash 物理格式 CRC

Flash 代码区使用 **32-byte data + 2-byte CRC16** 格式:
- CRC 算法: CRC-16/CCITT (poly 0x8005, init 0xFFFF, big-endian)
- 物理偏移公式: `physical = (logical / 32) * 34 + (logical % 32)`

### 5.3 OTA 校验

Bootloader 包含完整的 diff2ya/bspatch OTA 引擎，校验:
- 源固件 CRC32 (`src_crc32`)
- Patch 文件 CRC32 (`patch_crc32`)
- 目标固件 CRC32 (`dst_crc32`)

相关代码在 offset 0x68DB+ 区域，使用 `new_flash_crc32` 函数。

---

## 6. 多核唤醒

### 6.1 SDK 中的多核启动代码

根据 `system_main.c`，BK7258 支持 CPU0 + CPU1 + CPU2 三核:

```c
void reset_cpu1_core(uint32 offset, uint32_t start_flag) {
    sys_drv_set_cpu1_pwr_dw(0);           // 上电
    sys_drv_set_cpu1_rxevt_sel(1);         // 设置接收事件
    sys_drv_set_cpu1_boot_address_offset(offset >> 8);  // 设置启动地址
    sys_drv_set_cpu1_reset(start_flag);    // 释放复位
}

void start_cpu1_core(void) {
    uint32 addr = get_partition_addr(1);   // 获取 CPU1 app 分区地址
    reset_cpu1_core(SOC_FLASH_DATA_BASE + addr, 1);
    mb_ipc_reset_notify(1, 1);             // 通知 mailbox IPC
}
```

### 6.2 Tuya Bootloader 中的多核支持

从字符串分析，Tuya bootloader 有 `app`, `app1`, `app2` 三个分区:
- `app`: CPU0 主 app
- `app1`: CPU1 app
- `app2`: CPU2 app (如果存在)

但 bootloader 本身 **不负责唤醒 CPU1/CPU2**。多核启动由主 app (CPU0) 的 RTOS 代码负责:
- `start_cpu1_core()` 在 `system_main.c` 中
- `start_cpu2_core()` 需要 `CONFIG_CPU_CNT > 2`

### 6.3 CPU1 启动地址设置

```c
// 启动地址寄存器 (需要右移 8 位)
sys_drv_set_cpu1_boot_address_offset(addr >> 8);

// 复位释放
sys_drv_set_cpu1_reset(1);
```

这些操作在 bootloader 完成 app 跳转后，由 app 的 RTOS 初始化代码执行。

---

## 7. 跳转代码

### 7.1 跳转序列 (基于 SDK 和自定义 bootloader 验证)

```asm
; 1. 打印 "jump to:0x%x\n"
ldr   r0, =jump_str        ; 加载格式字符串
ldr   r1, =0x02010000       ; app 基址
bl    printf                 ; 打印跳转地址

; 2. 设置 VTOR (Vector Table Offset Register)
ldr   r0, =0xE000ED08       ; SCB->VTOR
ldr   r1, =0x02010000       ; app vector table 地址
str   r1, [r0]              ; VTOR = app base

; 3. 设置 MSP (Main Stack Pointer)
ldr   r6, =0x02010000       ; app base
ldr   r6, [r6]              ; r6 = app MSP (从 vector table [0])
msr   msp, r6               ; MSP = app MSP

; 4. 跳转到 app Reset_Handler
ldr   r7, =0x02010000       ; app base
ldr   r7, [r7, #4]          ; r7 = app Reset_Handler (从 vector table [1])
bx    r7                    ; 跳转 (bit0=1 表示 Thumb)
```

### 7.2 关键寄存器地址

| 寄存器 | 地址 | 含义 |
|--------|------|------|
| VTOR | `0xE000ED08` | 向量表偏移 (SCB + 0x08) |
| MSP | CPU 内部 | 主栈指针 (MSR 指令设置) |
| AIRCR | `0xE000ED0C` | 应用中断和复位控制 |

### 7.3 跳转后 CPU 状态 (实测基线)

根据自定义 bootloader 验证:
```
PC      = 0x02010246  (进入 app 代码区)
VTOR    = 0x02010000  (向量表已切到 app)
MSP     = 0x2809F700  (app 栈指针)
IPSR    = 0x000       (NoException)
PRIMASK = 0x01        (中断仍关闭)
```

---

## 8. 寄存器操作表

### 8.1 系统时钟寄存器 (SYS_REG @ 0x44010000)

| 偏移 | 地址 | 用途 | 备注 |
|------|------|------|------|
| 0x030 | `0x44010030` | Clock Enable 0 | UART1 clock = bit10 (0x400) |
| 0x0C0 | `0x440100C0` | GPIO0-7 peripheral mux | UART1 pinmux |
| 0x0C4 | `0x440100C4` | GPIO8-15 peripheral mux | |
| 0x0E0 | `0x440100E0` | DEBUG_CONFIG0 | SWD/debug 使能 |
| 0x0E4 | `0x440100E4` | DEBUG_CONFIG1 | SWD/debug 使能 |

### 8.2 AON PMU 寄存器 (@ 0x44000000)

| 偏移 | 地址 | 用途 |
|------|------|------|
| 0x400 | `0x44000400` | GPIO0 config |
| 0x404 | `0x44000404` | GPIO1 config |
| 0x600 | `0x44000600` | AON WDT control |

### 8.3 UART1 寄存器 (@ 0x45830000)

| 偏移 | 地址 | 用途 | 典型值 |
|------|------|------|--------|
| 0x008 | `0x45830008` | Global control | `0x00000001` (使能) |
| 0x010 | `0x45830010` | Config | `0x00003719` (TX使能, 8bit, clk_div=0x37) |
| 0x018 | `0x45830018` | FIFO status | (读取检查 TX FIFO 空) |
| 0x01C | `0x4583001C` | FIFO port | (写入发送数据) |

### 8.4 Flash 控制器 (@ 0x44030000)

| 偏移 | 地址 | 用途 |
|------|------|------|
| 0x010 | `0x44030010` | Status register (busy bit) |
| 0x018 | `0x44030018` | Data register |
| 0x028 | `0x44030028` | Config register |
| 0x034 | `0x44030034` | Read command |
| 0x054 | `0x44030054` | Address/command |

### 8.5 看门狗寄存器

| 地址 | 用途 | 关闭方法 |
|------|------|----------|
| `0x44000600` | AON WDT | Key sequence: 写 0x005A0000, 再写 0x00A50000 |
| `0x44800008` | APB WDT global_ctrl | 先写 1 使能 |
| `0x44800010` | APB WDT ctrl | Key sequence: 写 0x005A0000, 再写 0x00A50000 |

### 8.6 GPIO Golden State (UART1 输出)

| GPIO | 配置地址 | 值 | 含义 |
|------|---------|-----|------|
| GPIO0 | `0x44000400` | `0x00000078` | UART1 TX function |
| GPIO1 | `0x44000404` | `0x00150078` | UART1 RX function |
| GPIO0-7 mux | `0x440100C0` | `0x00000000` | peripheral mode |

---

## 9. App 加载主路径

### 9.1 完整路径 (从 flash 读取 -> 校验 -> 跳转)

```
1. FAL 初始化
   fal_mflash_init()
   ├── fal_flash_device_find("beken_onchip_crc")  -> flash 设备
   └── fal_mpartition_find("app")                 -> app 分区

2. 读取 App Header
   ty_adapt_flash_read(app_offset, buffer, header_size)
   ├── 从 flash 读取 0x200+ 字节
   └── 数据经过 CRC-expanded 解码 (32+2 -> 32)

3. 校验 App Header
   ├── MSP check: 0x28000000 <= MSP <= 0x280A0000
   ├── Reset check: Reset_Handler & 1 == 1
   ├── Magic check: *(0x02010100) == 0x32374B42 && *(0x02010104) == 0x00003633
   └── (可选) CRC32 check: 使用 beken_onchip 硬件 CRC

4. OTA 校验 (如果需要)
   ├── diff2ya_header.magic_ver
   ├── diff2ya_header.src_crc32
   └── diff2ya_header.dst_crc32

5. 跳转
   printf("jump to:0x%x\n", app_base)
   SCB->VTOR = 0x02010000
   MSP = *(uint32_t*)0x02010000
   BX  = *(uint32_t*)0x02010004 | 1
```

### 9.2 Flash 读取流程

Bootloader 使用 Beken 的 flash 控制器进行读取:
1. 等待 flash 控制器空闲 (status register bit check)
2. 设置读命令到 command register
3. 设置地址到 address register
4. 启动传输
5. 从 data register 读取数据

关键代码模式:
```asm
; 等待 flash 空闲
ldr   r3, [r2, #16]     ; 读 status
cmp   r3, #0
blt   wait_loop          ; 如果 busy, 继续等待

; 发送读命令
mov.w r0, #0x03000000    ; read command
str   r0, [r2, #84]      ; 写 command register
; ... 设置地址, 启动传输
```

---

## 10. 与 BK 官方样本的可确认比较

### 10.1 地址视图必须区分

| 项目 | BK 官方 normal bootloader 样本 | 涂鸦样本 | 结论 |
|---|---|---|---|
| Bootloader magic | logical `0x100` | packed physical `0x110` | 均对应 logical `0x100` |
| Magic 值 | `BK7236\x10\x00` | `BK7236\x10\x00` | 相同 |
| MSP | `0x28030000` | `0x28030000` | 相同 |
| Reset_Handler | `0x020001C1` | `0x020001C1` | 相同 |

涂鸦样本以 32-byte data + 2-byte CRC16 的物理展开格式观察时，logical `0x100`
映射为 physical `0x110`。因此不能把 `0x110` 与官方逻辑镜像中的 `0x100`
解释成两套不同的 magic 契约。

### 10.2 比较边界

该样本包含 FAL、差分升级和 UART 输出相关字符串，但字符串和线性反汇编不能单独证明
完整运行时调用链。官方 startup 源码、官方 normal bootloader 二进制与涂鸦样本也不是
同一种证据对象；不能据此维护“谁支持 OTA”或“完整功能差异”的现行表格。

### 10.3 可复用的格式结论

1. **Bootloader magic**：`BK7236\x10\x00` @ logical offset `0x100`
2. **App magic**：`BK7236\0\0` @ logical offset `0x100`
3. **Flash format**：32-byte data + 2-byte CRC16（big-endian）
4. **MSP range**：`0x28000000 - 0x280A0000`
5. **Reset_Handler**：Thumb address（bit0 = 1）
6. **VTOR**：`0xE000ED08`

---

## 11. 对"自己写 Bootloader"的可复用结论

### 11.1 必须满足的格式要求

```c
// 1. 向量表 @ offset 0x000
struct {
    uint32_t sp;           // 0x28030000 或其他 SRAM 地址
    uint32_t reset;        // Reset_Handler | 1 (Thumb)
    uint32_t nmi;          // 死循环
    uint32_t hardfault;    // 死循环
    // ... 其他异常向量
} vector_table;

// 2. Boot magic @ offset 0x100 (logical)
uint8_t boot_magic[8] = {0x42, 0x4B, 0x37, 0x32, 0x33, 0x36, 0x10, 0x00};
// 即 "BK7236" + 0x10 + 0x00

// 3. 代码从 offset 0x120+ 开始
```

### 11.2 最小 Bootloader 模板

```asm
.section .text
.global _start

; 向量表 @ 0x000
.word 0x2809F700          ; MSP
.word _start + 1          ; Reset_Handler (Thumb)
.word _nmi + 1            ; NMI
.word _hf + 1             ; HardFault
; ... 填充到 0x100

; Boot magic @ 0x100
.ascii "BK7236"
.byte 0x10, 0x00

; 填充到 0x120
.org 0x120

_start:
    cpsid i               ; 关闭中断

    ; 关闭看门狗
    ldr r0, =0x44000600
    ldr r1, =0x005A0000
    str r1, [r0]
    ldr r1, =0x00A50000
    str r1, [r0]

    ; 初始化 UART1
    ; ... (详见 12-custom-bootloader.md)

    ; 读取 app header
    ldr r5, =0x02010000   ; app base
    ldr r6, [r5]          ; app MSP
    ldr r7, [r5, #4]      ; app Reset_Handler

    ; 校验 MSP
    ldr r0, =0x28000000
    cmp r6, r0
    blt bad_app
    ldr r0, =0x280A0000
    cmp r6, r0
    bhi bad_app

    ; 校验 Thumb bit
    movs r0, #1
    ands r0, r7
    beq bad_app

    ; 校验 Magic
    ldr r4, =0x02010100
    ldr r0, [r4]
    ldr r1, =0x32374B42   ; "BK72" little-endian
    cmp r0, r1
    bne bad_app
    ldr r0, [r4, #4]
    ldr r1, =0x00003633   ; "36\0\0" little-endian
    cmp r0, r1
    bne bad_app

    ; 跳转到 app
    ldr r0, =0xE000ED08   ; VTOR
    ldr r1, =0x02010000   ; app base
    str r1, [r0]          ; 设置 VTOR
    msr msp, r6           ; 设置 MSP
    bx  r7                ; 跳转 (bit0=1, Thumb)

bad_app:
    ; 错误处理 (死循环或恢复)
    b bad_app

_nmi:
_hf:
    b _nmi                ; 异常死循环
```

### 11.3 CRC-expanded 打包

```python
# CRC-16/CCITT 算法
def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x8005
            else:
                crc <<= 1
    return crc & 0xFFFF

# 打包: 32 bytes data + 2 bytes CRC16 (big-endian)
def pack_crc(data):
    result = bytearray()
    for i in range(0, len(data), 32):
        block = data[i:i+32]
        if len(block) < 32:
            block += b'\x00' * (32 - len(block))
        crc = crc16(block)
        result.extend(block)
        result.extend(struct.pack('>H', crc))
    return bytes(result)
```

### 11.4 必须避免的坑

1. **GPIO golden state**: UART1 输出依赖 GPIO0/1 的正确配置 (`0x78` / `0x00150078`)
2. **WDT key sequence**: 不能简单写 0，必须用 `0x5A` / `0xA5` 序列
3. **Flash CRC format**: 代码区必须用 32+2 CRC-expanded 格式
4. **MSP 选择**: 必须在 SRAM 范围内 (`0x28000000-0x280A0000`)
5. **VTOR 必须设置**: 否则异常会跳回 bootloader 的向量表
6. **PRIMASK**: `cpsid i` 后不会自动恢复，app 需要自己 `cpsie i`

### 11.5 验证检查清单

- [ ] Boot magic @ logical 0x100 = `BK7236\x10\x00`
- [ ] MSP @ 0x000 在 SRAM 范围
- [ ] Reset_Handler @ 0x004 的 bit0 = 1
- [ ] Physical flash offset 计算正确 (32+2 格式)
- [ ] UART1 clock gate 已使能 (0x44010030 bit10)
- [ ] GPIO0/1 配置正确 (golden state)
- [ ] WDT 已关闭/喂狗
- [ ] Debug/SWD 已使能 (0x440100E0/E4)
- [ ] VTOR 已设置为 app base
- [ ] MSP 已设置为 app MSP
- [ ] BX 使用 app Reset_Handler (bit0=1)

---

## 附录 A: 关键地址速查表

| 地址 | 用途 |
|------|------|
| `0x02000000` | Bootloader XIP base |
| `0x02010000` | App XIP base |
| `0x28000000` | SRAM base (640KB) |
| `0x28030000` | Bootloader MSP |
| `0x280A0000` | SRAM top |
| `0x44000000` | AON PMU base |
| `0x44000400` | GPIO config base |
| `0x44000600` | AON WDT |
| `0x44010000` | SYS_REG base |
| `0x44010030` | Clock enable |
| `0x440100E0` | Debug config |
| `0x44030000` | Flash controller |
| `0x44800000` | APB WDT |
| `0x45830000` | UART1 base |
| `0xE000ED08` | VTOR (SCB) |

## 附录 B: 字符串表 (关键)

| 偏移 | 字符串 | 用途 |
|------|--------|------|
| 0x7EFA | `" u_bootloader enter"` | Bootloader 入口日志 |
| 0x7F7E | `"jump to:0x%x\n"` | 跳转 app 日志 |
| 0x7F48 | `"beken_onchip_crc"` | Flash CRC 驱动名 |
| 0x800E | `"fal_flash_device_find"` | FAL 设备查找 |
| 0x802A | `"fal_mflash_init"` | FAL flash 初始化 |
| 0x804C | `"fal_mpartition_find"` | FAL 分区查找 |
| 0x81C4 | `"PEbootloader"` | 分区: bootloader |
| 0x8208 | `"PEapp"` | 分区: app |
| 0x824C | `"PEapp1"` | 分区: app1 |
| 0x8290 | `"PEapp2"` | 分区: app2 |
| 0x82D4 | `"PEdownload"` | 分区: download |

## 附录 C: OTA 相关字符串

| 偏移 | 字符串 |
|------|--------|
| 0x6A4A | `"diff2ya_header.magic_ver = 0x%x"` |
| 0x6AAE | `"diff2ya_header.bin_type = 0x%x"` |
| 0x6AD1 | `"diff2ya_header.src_crc32 = 0x%x"` |
| 0x6AF5 | `"diff2ya_header.dst_crc32 = 0x%x"` |
| 0x7208 | `"BSPATCH_HEADER_LEN read fail1!"` |
| 0x722B | `"DIFF2YA0"` |
| 0x7766 | `"######## OTA successd! ########"` |
| 0x77EF | `"#################### ota successd! #####################"` |

## 附录 D: 反汇编片段

### D.1 Flash 读取函数 (offset 0x0C00)

```asm
; Flash 控制器初始化
02000C00: mov.w r1, #0x14000000   ; read command
02000C04: str   r1, [r3, #84]     ; 写 command register (offset 0x54)
02000C06: ldr.w ip, [r3, #16]     ; 读 status
02000C0A: orr.w r2, ip, #0x60000000 ; 设置传输模式
02000C0E: str   r2, [r3, #16]     ; 写 status

; 等待 flash 空闲
02000C10: ldr   r3, [pc, #36]    ; flash base
02000C12: ldr   r0, [r3, #16]    ; 读 status
02000C14: ble   error
02000C16: cmp   r0, #0
02000C18: blt   0x2000C14         ; busy loop
```

### D.2 函数入口 @ 0x15C (系统初始化)

```asm
; 加载寄存器基址
0200015C: ldr   r3, [pc, #88]    ; r3 = peripheral base (literal pool 0x20001B8)
0200015E: ldr.w ip, [pc, #92]    ; ip = another base (literal pool 0x20001BC)

; 检查并清除某个状态位
02000162: ldr   r2, [r3, #64]    ; 读 reg[0x40]
02000164: lsls  r1, r2, #28      ; 检查 bit3
02000166: ittt  mi               ; 如果 bit3 置位
02000168: ldrmi r2, [r3, #64]    ; 重读
0200016A: bicmi.w r2, r2, #8     ; 清除 bit3
0200016E: strmi r2, [r3, #64]    ; 写回

; 设置某个配置位
02000170: ldr   r0, [r3, #48]    ; 读 reg[0x30]
02000172: lsls  r1, r0, #16      ; 检查 bit15
02000174: movs  r0, #99
02000176: ittt  pl               ; 如果 bit15 清除
02000178: ldrpl r1, [r3, #48]    ; 重读
0200017A: orrpl.w r1, r1, #0x8000 ; 设置 bit15
0200017E: strpl r1, [r3, #48]    ; 写回
```

### D.3 Reset_Handler @ 0x1C0

```asm
; 注意: 0x1C0-0x1C9 可能是数据/对齐填充
; 实际 Reset_Handler 可能从 0x1CA 开始

020001CA: movs  r0, #1           ; 参数1 = 1
020001CC: push  {r3, lr}         ; 保存寄存器
020001CE: bl    0x2004E6A        ; 调用初始化函数1
020001D2: bl    0x2004892        ; 调用初始化函数2
020001D6: b.n   0x20001D6        ; 无限循环 (不应该到达)
```

### D.4 UART 输出函数 @ 0x0AA4

```asm
; 等待 TX FIFO 空
02000AA4: ldr   r3, [pc, #8]     ; r3 = UART1 base (0x45830000)
02000AA6: ldr   r2, [r3, #24]    ; 读 FIFO status (offset 0x18)
02000AA8: lsls  r2, r2, #11      ; 检查 TX 空标志
02000AAA: bpl   0x2000AA6         ; 未空则继续等待

; 写入数据
02000AAC: str   r0, [r3, #28]    ; 写 FIFO port (offset 0x1C)
02000AAE: bx    lr               ; 返回
```

---

*本文档基于只读逆向分析生成，未修改任何源码/二进制/SDK。*
