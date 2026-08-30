# BK7258 官方 Bootloader 完整逆向分析

> 目标二进制: `bk7258/bootloader/normal_bootloader/bootloader.bin` (52352 bytes)
> 处理器: Cortex-M33 (Thumb-2), 链接基址 0x02000000
> 分析工具: arm-none-eabi-objdump, xxd, strings
> 反汇编: `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x02000000`

> **2026-07-31 Ghidra 复核说明：** 本文最初基于线性 objdump，标题中的“完整”只表示
> 当时覆盖了整个文件，不代表已经语义等价复刻所有 52 KB 功能。技术支持 SDK
> v3.1.1.9 的 exact binary 为 52352 bytes，SHA-256
> `105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6`，
> version `bc31115`，base `0x02000000`、SP `0x28030000`、reset
> `0x020001c1`。Ghidra 当前在另行种入 FAL 入口后识别 140 个函数，并确认 reset/handoff
> 包含早期 SoC、flash、WDT、UART、clock、MSPLIM、runtime、cache/MPU 清理和 app
> 跳转。项目只 clean-room 复现 raw NuttX 启动必需的硬件契约，没有移植官方
> RBL/OTA/download 全部协议。
>
> **地址勘误：SCB 正确基址是 `0xE000ED00`。** 旧版文档中的
> `0xED00E000` 是字节顺序写错，以下内容已统一更正。对函数边界和语义有疑问时，
> 以 Ghidra 工程、exact binary 和当前源码交叉验证结果为准。
>
> **2026-08-07 Reset-path 勘误：** 从 `0x020001c0` 强制建立 Cortex-M reset
> 入口后可直接确认：初始 MSP 是 vector[0] 的 `0x28030000`，而 MSPLIM literal 是
> `0x2802f800`，不是旧文写的 `0x28000000`。normal boot 的默认 reset 路径完成
> early init 后直接以 `0x02010000` 调用 `0x02001720`，由该函数设置 VTOR/MSP 并跳入
> CP vector；`0x02001444` 的 FAL/FOTA entry 不在这条直接 Reset call graph 中。下文
> 将它描述为默认启动流程的旧段落只能作为历史反汇编笔记，不能据此声称 normal boot
> 会在每次 cold reset 先运行完整 FOTA/RBL 分区选择。

---

## 1. 已直接确认的默认 cold-reset 流程

```
上电 / 复位
│
├─ BootROM (ROM, 不在本文件中)
│  ├─ 初始化 OTP / Flash 基础读取
│  ├─ 校验 flash 0x0 的 bootloader magic (BK7236\x10\x00)
│  └─ 跳转到 0x02000004 (Reset_Handler)
│
▼
Reset_Handler (0x020001C1, Thumb bit set)
│
├─ [1] hw_early_init (0x2000148)
│  ├─ SYS_REG (0x44010000+0x40): 配置时钟分频
│  ├─ SYS_REG (0x44010000+0x30): 配置 PLL 参数
│  ├─ SYS_REG+0x10000 (0x44020000+0x2C8): 检查并配置 flash 控制器就绪
│  └─ SYS_REG+0x10000 (0x44020000+0x7C8): 检查启动模式
│
├─ [2] flash_init (0x200102A → 0x2000FE4)
│  ├─ WDT 解锁: 写 0x005A0000 和 0x00A50000 到 0x44800010
│  ├─ 设置 flash 时钟分频
│  └─ 配置 flash 读取模式 (single/quad)
│
├─ [3] uart_init (0x20003B0, UART1)
│  ├─ 配置 GPIO pinmux
│  ├─ 设置波特率 115200
│  └─ 使能 UART 发送
│
├─ [4] sys_clk_init (0x200073C)
│  ├─ 配置 PLL (0x44010000 区域)
│  ├─ 等待 PLL 锁定
│  └─ 切换系统时钟到 PLL
│
├─ [5] 设置 MSPLIM (0x2802F800)
│  └─ msr MSPLIM, #0x2802F800
│
├─ [6] flash_ctrl_config (0x2000280)
│  ├─ 配置 flash 控制器寄存器
│  └─ 使能 flash cache
│
├─ [7] cache/runtime 初始化表
│  ├─ `check_xip_mode()` 决定是否执行 data/bss init tables
│  └─ 这条路径没有调用 FAL/FOTA entry
│
└─ [8] fixed_handoff (0x2001720, r0 = 0x02010000)
   ├─ SCB->VTOR = 0x02010000
   ├─ ldrd MSP, Reset_Handler from [0x02010000]
   ├─ 清空 r0-r12
   └─ bx Reset_Handler → 进入 CP app
│
└─ 完成, CP app 开始执行
```

`0x02001444`（FAL/download entry）与 `0x020017d0`（分区读取后 handoff）确实存在，
但并不在上述 Reset Handler 的直接调用链上。它们的触发条件、下载协议和完整错误
分支仍是独立的逆向课题，不能冒充 normal cold reset 的默认启动逻辑。

---

## 2. 关键函数反汇编片段 + 注释

### 2.1 向量表 (0x02000000)

```asm
02000000: 0000 0328    ; SP = 0x28030000 (SRAM 顶部)
02000004: c101 0002    ; Reset_Handler = 0x020001C1 (Thumb)
02000008: 4301 0002    ; NMI_Handler = 0x02000143 (死循环)
0200000c: 4101 0002    ; HardFault_Handler = 0x02000141 (死循环)
02000010: 4501 0002    ; MemManage_Handler = 0x02000145 (死循环)
02000014: 4501 0002    ; BusFault_Handler
02000018: 4501 0002    ; UsageFault_Handler
0200001c: 4501 0002    ; SecureFault_Handler
...
0200003c: f517 0002    ; SysTick_Handler = 0x020017F5
```

### 2.2 Bootloader Magic (0x02000100)

```
0x100: 42 4B 37 32 33 36 10 00  = "BK7236" + 0x10 + 0x00
```

BootROM 校验此 magic 来确认 bootloader 合法性。格式: 8 字节, `BK7236\x10\x00`。

### 2.3 Reset_Handler (0x020001C0)

```asm
020001C0: b538         push    {r3,r4,r5,lr}
020001C2: fff7 c1ff    bl      0x2000148    ; hw_early_init (时钟/PLL)
020001C6: f000 30ff    bl      0x200102a    ; flash_init + WDT
020001CA: 2001         movs    r0, #1       ; UART ID = 1
020001CC: f000 f0f0    bl      0x20003b0    ; uart_init(1)
020001D0: 2002         movs    r0, #2       ; clock source = 2
020001D2: f000 b3fa    bl      0x200073c    ; sys_clk_init(2)
020001D6: 4b23         ldr     r3, =0x2802F800
020001D8: 83f3 0a88    msr     MSPLIM, r3   ; 设置栈下限保护
020001DC: f000 50f8    bl      0x2000280    ; flash_ctrl_config
020001E0: 2000         movs    r0, #0
020001E2: 2149         ldr     r1, =0xE000ED00  ; SCB base
020001E4: c1f8 8400    str.w   r0, [r1,#0x84]   ; SCB->CCR = 0
020001E8: bff3 4f8f    dsb     sy
...
02000216: f3bf 8f4f    dsb     sy
0200021A: f3bf 8f6f    isb     sy
0200021E: f000 8787    bl      0x2000330    ; check_xip_mode
02000222: 2801         cmp     r0, #1
02000224: d018         beq     0x2000258    ; if XIP, skip flash init
02000226: 4c11         ldr     r4, =0x200C358  ; init function table start
02000228: 4d11         ldr     r5, =0x200C370  ; init function table end
0200022A: e006         b       0x200023a
0200022C: e9d4 0101    ldrd    r0,r1,[r4,#4]   ; load args
02000230: 008a         lsls    r2, r1, #2       ; size
02000232: f854 1b0c    ldr.w   r1,[r4],#12      ; load addr + advance
02000236: f006 847     bl      0x20062c8        ; memcpy
0200023A: 42ac         cmp     r4, r5           ; table end?
0200023C: d3f6         bcc     0x200022c        ; loop
0200023E: ...          ; 继续到 heap_init 等
```

### 2.4 WDT 初始化/喂狗 (0x2000FE4 / 0x2001010)

```asm
; sub_2000FE4: WDT 初始化
; 参数: r0 = 超时值 (内部使用)
02000FE4: f04f 4288    mov.w   r2, #0x44000000  ; SYS_REG base
02000FE8: 6893         ldr     r3, [r2, #8]     ; read SYS_CTRL
02000FEA: f023 013f    bic.w   r1, r3, #0x3F    ; clear WDT bits
02000FEE: f041 0c26    orr.w   ip, r1, #0x26    ; set WDT config
02000FF2: f04f 4189    mov.w   r1, #0x44800000  ; WDT base (SOC_WDT_REG_BASE)
02000FF6: f440 03b4    orr.w   r3, r0, #0x5A0000 ; unlock key 1
02000FFA: f440 0025    orr.w   r0, r0, #0xA50000 ; unlock key 2
02000FFE: f8c2 c008    str.w   ip, [r2, #8]     ; SYS_CTRL = config
02001002: 610b         str     r3, [r1, #0x10]   ; WDT_UNLOCK = key1
02001004: 6108         str     r0, [r1, #0x10]   ; WDT_UNLOCK = key2
02001006: f8c2 3600    str.w   r3, [r2, #0x600]  ; AON_WDT key1
0200100A: f8c2 0600    str.w   r0, [r2, #0x600]  ; AON_WDT key2
0200100E: 4770         bx      lr

; sub_2001010: WDT 喂狗/复位
02001010: f04f 4288    mov.w   r2, #0x44000000  ; SYS_REG base
02001014: b508         push    {r3,lr}
02001016: f8d2 3104    ldr.w   r3, [r2, #0x104]  ; read WDT status
0200101A: f023 0003    bic.w   r0, r3, #3        ; clear WDT flags
0200101E: f8c2 0104    str.w   r0, [r2, #0x104]  ; write back
02001022: 2006         movs    r0, #6             ; WDT_FEED value
02001024: f7ff ffde    bl      0x2000fe4          ; re-init WDT
02001028: e7fe         b.n     0x2001028          ; 死循环 (不应到达)
```

### 2.5 UART 初始化 (0x2000930, UART0 @ 115200)

```asm
; sub_2000930: UART0 初始化
; 参数: r0 = 波特率 (0x1C200 = 115200)
02000930: 2201         movs    r2, #1
02000932: 2100         movs    r1, #0
02000934: b538         push    {r3,r4,r5,lr}
02000936: 4b1a         ldr     r3, =0x44010000   ; SYS_REG base
02000938: 609a         str     r2, [r3, #8]      ; 使能 UART 时钟
0200093A: 6119         str     r1, [r3, #0x10]   ; 清除中断
0200093C: b370         cbz     r0, 0x200099c      ; 波特率=0则用默认
0200093E: 4c19         ldr     r4, =0x018CBA7F   ; 时钟频率常量
02000940: f641 7cff    movw    ip, #0x1FFF        ; 最大分频
02000944: eb04 0550    add.w   r5, r4, r0, lsr #1 ; 四舍五入
02000948: fbb5 f0f0    udiv    r0, r5, r0         ; 计算分频值
0200094C: 1e44         subs    r4, r0, #1
0200094E: 2c04         cmp     r4, #4
02000950: bf38         it      cc
02000952: 2404         movcc   r4, #4             ; 最小分频=4
02000954: 4564         cmp     r4, ip
02000956: bf28         it      cs
02000958: 4664         movcs   r4, ip             ; 最大分频=0x1FFF
0200095A: 4d13         ldr     r5, =0x44010000    ; SYS_REG base
0200095C: 2000         movs    r0, #0
0200095E: f8d5 e030    ldr.w   lr, [r5, #0x30]   ; read clock config
02000962: 0224         lsls    r4, r4, #8         ; 分频值移位
02000964: f04e 0304    orr.w   r3, lr, #4         ; 使能 UART 时钟门
02000968: 632b         str     r3, [r5, #0x30]    ; write clock config
0200096A: f7ff f9b     bl      0x20008a4          ; GPIO pinmux
0200096E: f44f 5201    mov.w   r2, #0x2040        ; UART config
02000972: 490b         ldr     r1, =0x44010000
02000974: 2042         movs    r0, #0x42          ; 8N1
02000976: 614a         str     r2, [r1, #0x14]    ; UART_CTRL
02000978: 2200         movs    r2, #0
0200097A: f044 031b    orr.w   r3, r4, #0x1B     ; 波特率 + 使能
0200097E: 6208         str     r0, [r1, #0x20]    ; UART_FORMAT
02000980: 628a         str     r2, [r1, #0x28]    ; UART_FIFO_CTRL
02000982: 480a         ldr     r0, =0x28000144
02000984: 62ca         str     r2, [r1, #0x2C]    ; UART_INT_CTRL
02000986: 610b         str     r3, [r1, #0x10]    ; UART_BAUD = config
02000988: ...
```

### 2.6 Flash 读取函数 (0x2000EC4)

```asm
; sub_2000EC4: 从 flash 读取数据
; 参数: r0 = dest, r1 = flash_addr, r2 = length
02000EC4: e92d 41f0    stmdb   sp!, {r4-r8,lr}
02000EC8: b382         cbz     r2, done           ; length=0, return
02000ECA: 4c19         ldr     r4, =0x44000000    ; flash controller base
02000ECC: 6923         ldr     r3, [r4, #0x10]   ; flash status
02000ECE: 2b00         cmp     r3, #0
02000ED0: dbfc         blt     0x2000ecc          ; 等待 flash 就绪
; 设置 flash 地址
02000ED2: f021 457f    bic.w   r5, r1, #0xFF000000 ; 清除高位
02000EDA: f045 67a0    orr.w   r7, r5, #0x05000000 ; 设置读命令
02000EDE: f8dc 6010    ldr.w   r6, [ip, #0x10]   ; 等待就绪
02000EE2: f8cc 7054    str.w   r7, [ip, #0x54]   ; flash address reg
02000EE6: f8dc 4010    ldr.w   r4, [ip, #0x10]
02000EEA: f006 4380    and.w   r3, r6, #0x40000000
02000EEE: 4323         orrs    r3, r4
02000EF0: f043 5600    orr.w   r6, r3, #0x20000000 ; 触发读
02000EF4: f8cc 6010    str.w   r6, [ip, #0x10]
; 读取 32 字节数据
02000F00: 466c         mov     r4, sp
02000F02: 2600         movs    r6, #0
02000F04: 4623         mov     r3, r4
02000F06: 3120         adds    r1, #0x20          ; flash_addr += 32
02000F08: f8dc 7018    ldr.w   r7, [ip, #0x18]   ; flash data reg
02000F0C: 3601         adds    r6, #1
02000F0E: 2e08         cmp     r6, #8             ; 8 x 4 bytes = 32
02000F10: f843 7b04    str.w   r7, [r3], #4
02000F14: d1f8         bne     0x2000f08          ; 循环读取
; 拷贝到目标
02000F16: f100 0520    add.w   r5, r0, #0x20
02000F1A: f814 3b01    ldrb.w  r3, [r4], #1
02000F1E: 3a01         subs    r2, #1
02000F20: f800 3b01    strb.w  r3, [r0], #1
02000F24: d002         beq     done
02000F26: 42a8         cmp     r0, r5
02000F28: d1f7         bne     0x2000f1a
02000F2A: e7d4         b       0x2000ed6          ; 读取下一组
done:
02000F2C: b009         add     sp, #0x24
02000F2E: bdf0         pop     {r4-r7,pc}
```

### 2.7 Jump to App 函数 (0x200176C)

```asm
; sub_200176C: 跳转到 app
; 参数: r0 = app 向量表地址
0200176C: b570         push    {r4,r5,r6,lr}
0200176E: 4604         mov     r4, r0              ; r4 = app_vec_addr
02001770: 4601         mov     r1, r0
02001772: 4815         ldr     r0, =0x02006DF4     ; " jump toxx:0x%x"
02001774: f7ff fc5c    bl      uart_printf
02001778: e9d4 5600    ldrd    r5, r6, [r4]       ; r5=MSP, r6=Reset
0200177C: f7ff f884    bl      uart_deinit
02001780: 2000         movs    r0, #0
02001782: f000 f8ad    bl      flash_cache_disable
02001786: 4b11         ldr     r3, =0xE000ED00     ; SCB base
02001788: 609c         str     r4, [r3, #8]        ; SCB->VTOR = app_vec
0200178A: f3bf 8f4f    dsb     sy
0200178E: f3bf 8f6f    isb     sy
02001792: 46b1         mov     r9, r6              ; r9 = Reset_Handler
02001794: f385 8808    msr     MSP, r5             ; MSP = app SP
02001798: f3bf 8f4f    dsb     sy
0200179C: f3bf 8f6f    isb     sy
; 清空所有通用寄存器
020017A0: f04f 0000    mov.w   r0, #0
020017A4: 4601         mov     r1, r0
020017A6: 4602         mov     r2, r0
020017A8: 4603         mov     r3, r0
020017AA: 4604         mov     r4, r0
020017AC: 4605         mov     r5, r0
020017AE: 4606         mov     r6, r0
020017B0: 4607         mov     r7, r0
020017B2: 4680         mov     r8, r0
020017B4: 4682         mov     sl, r0
020017B6: 4683         mov     fp, r0
020017B8: 4684         mov     ip, r0
020017BA: f3bf 8f4f    dsb     sy
020017BE: f3bf 8f6f    isb     sy
; 跳转
020017C2: 4748         bx      r9                  ; bx Reset_Handler
020017C4: e7fe         b.n     0x20017c4           ; 死循环 (安全)
```

### 2.8 App 加载主路径 (0x20017D0)

```asm
; sub_20017D0: 加载 app 分区并跳转
020017D0: b508         push    {r3,lr}
020017D2: 4807         ldr     r0, =0x02006E06     ; "flash id:" or partition name
020017D4: f001 ffce    bl      partition_get_info   ; 获取分区信息
020017D8: 2322         movs    r3, #0x22           ; 34 (CRC expansion factor)
020017DA: 6b40         ldr     r0, [r0, #0x34]     ; 分区 offset (逻辑扇区)
020017DC: 0141         lsls    r1, r0, #5           ; offset * 32
020017DE: fb91 f2f3    sdiv    r2, r1, r3           ; (offset * 32) / 34
020017E2: f102 7000    add.w   r0, r2, #0x02000000 ; + flash_base
020017E6: f7ff ffc1    bl      jump_to_app          ; 跳转!
020017EA: 2000         movs    r0, #0
020017EC: bd08         pop     {r3,pc}
```

### 2.9 Flash Cache 配置 (0x20018E0)

```asm
; sub_20018E0: 配置 flash cache
; 参数: r0 = 0 (disable) 或 1 (enable+configure)
020018E0: b538         push    {r3,r4,r5,lr}
020018E2: 4c38         ldr     r4, =0xE000ED00     ; SCB base
020018E4: bb30         cbnz    r0, enable_path
; disable path:
020018E6: f8c4 0084    str.w   r0, [r4, #0x84]     ; SCB->CCR = 0
020018EA: f3bf 8f4f    dsb     sy
020018EE: f8dc c014    ldr.w   ip, [r4, #0x14]     ; SCB->SHCSR
020018F2: f42c 3580    bic.w   r5, ip, #0x10000    ; 清除 cache enable
020018F6: 6165         str     r5, [r4, #0x14]
020018F8: f3bf 8f4f    dsb     sy
; 配置 cache 区域
020018FC: f8d4 e080    ldr.w   lr, [r4, #0x80]     ; cache config
02001900: f3ce 304e    ubfx    r0, lr, #13, #15    ; 提取 region size
02001904: f3ce 01c9    ubfx    r1, lr, #3, #10     ; 提取 base
02001908: 0143         lsls    r3, r0, #5
0200190A: 460a         mov     r2, r1
0200190C: f403 5cff    and.w   ip, r3, #0x1FE0
02001910: ea4c 7582    orr.w   r5, ip, r2, lsl #30
02001914: 3a01         subs    r2, #1
02001916: f8c4 5274    str.w   r5, [r4, #0x274]    ; 写 cache region
0200191A: d2f9         bcs     0x2001910
0200191C: 3b20         subs    r3, #0x20
0200191E: f113 0f20    cmn.w   r3, #0x20
02001922: d1f2         bne     0x200190a
02001924: f3bf 8f4f    dsb     sy
02001928: f3bf 8f6f    isb     sy
0200192C: e8bd 4038    ldmia.w sp!, {r3,r4,r5,lr}
02001930: f7ff bfc6    b.w     cache_invalidate
```

### 2.10 CRC16 表初始化 (0x2001414)

```asm
; sub_2001414: 生成 CRC16 查找表
; 多项式: 0x8320, 256 项
02001414: 2200         movs    r2, #0              ; i = 0
02001416: b530         push    {r4,r5,lr}
02001418: 4908         ldr     r1, =0x28000168     ; CRC table base (SRAM)
0200141A: 4c09         ldr     r4, =0x8320         ; CRC16 polynomial
0200141C: 4613         mov     r3, r2              ; crc = i
0200141E: 2008         movs    r0, #8              ; 8 bits per byte
; 内循环: 处理每个 bit
02001420: f003 0501    and.w   r5, r3, #1          ; check LSB
02001424: 085b         lsrs    r3, r3, #1          ; crc >>= 1
02001426: b105         cbz     r5, skip_xor
02001428: 4063         eors    r3, r4              ; crc ^= polynomial
skip_xor:
0200142A: 3801         subs    r0, #1
0200142C: d1f8         bne     0x2001420           ; loop 8 times
; 存储结果
0200142E: 3201         adds    r2, #1              ; i++
02001430: f5b2 7f80    cmp.w   r2, #0x100          ; 256 entries
02001434: f841 3b04    str.w   r3, [r1], #4        ; table[i] = crc
02001438: d1f0         bne     0x200141c           ; loop
0200143A: bd30         pop     {r4,r5,pc}
```

### 2.11 CRC32 计算函数 (0x2001EF4)

```asm
; sub_2001EF4: CRC32 计算
; 参数: r0 = initial_crc, r1 = data_ptr, r2 = length
; 返回: r0 = CRC32
02001EF4: 2200         movs    r2, #0
02001EF6: b530         push    {r4,r5,lr}
02001EF8: 4908         ldr     r1, =0x28000574     ; CRC32 table base
02001EFA: 440a         add     r2, r1              ; end = data + length
02001EFC: 4291         cmp     r1, r2              ; while (data < end)
02001EFE: d101         bne     loop
02001F00: 43c0         mvns    r0, r0              ; return ~crc
02001F02: bd10         pop     {r4,pc}
loop:
02001F04: f811 3b01    ldrb.w  r3, [r1], #1       ; byte = *data++
02001F08: 4043         eors    r3, r0              ; byte ^= crc
02001F0A: fa5f fc83    uxtb.w  ip, r3              ; index = byte & 0xFF
02001F0E: f854 302c    ldr.w   r3, [r4, ip, lsl #2] ; table[index]
02001F12: ea83 2010    eor.w   r0, r3, r0, lsr #8  ; crc = table[idx] ^ (crc>>8)
02001F16: e7f1         b       0x2000efc           ; next byte
```

### 2.12 v3.1.1.9 RBL body校验函数 (0x2001F1C)

> **2026-08-03 N15勘误：**旧稿把这一格式误写成`magic@+0x08`、SHA-256和variable
> signature。对exact v3.1.1.9 packager source与normal/AB binary重新交叉验证后，确认该描述
> 不成立。下面是当前可复现的准确结构。

| offset | size | field |
|---:|---:|---|
| `0x00` | 4 | `RBL\0` |
| `0x04` | 2 | algorithm |
| `0x06` | 6 | timestamp bytes |
| `0x0c` | 16 | application partition name |
| `0x1c` | 24 | download version |
| `0x34` | 24 | current version |
| `0x4c` | 4 | stored-body CRC32 |
| `0x50` | 4 | raw-body FNV-1a |
| `0x54` | 4 | raw size |
| `0x58` | 4 | stored-body size |
| `0x5c` | 4 | header CRC32 over前92 bytes |

`0x02001adc`读取完整`0x60` bytes header。`0x02001f1c`本身是normal download partition的
stored-body CRC32 verifier：从payload offset `0x60`分块读取，和header `+0x4c`比较；它不是
此前所称的header结构解析器。`0x02002050/0x02002070`计算32-bit FNV-1a，并按header
`+0x54` raw size与`+0x50` expected hash比较。

该格式没有SHA-256字段、公钥或签名。CRC32/FNV-1a只能用于非加密完整性检查，不得表述为
publisher authenticity。exact source与工具复核见
[N15 OTA source verification](../nuttx-port/n15-ota-source-verification.md)。

### 2.13 normal FAL 差分下载头不是 RBL，也不是 `FOTAL`

仅在独立的 `0x02001444` FAL/download 入口所调用的 `0x02002970` 中，binary 从
`download` 分区读取另一个固定 `0x44` bytes 的 diff-FOTA 头。首 4 bytes 必须是
ASCII `bkbl`，`+0x40` 是前 `0x40` bytes 的 CRC32，`+0x1c/+0x20` 分别是 payload
长度及其 CRC32。`+0x04/+0x08` 在应用前校验现有 preimage 的长度/CRC32（失败输出
`file version not match!`）；`+0x10` 约束目标产生文件的大小。`+0x24` 必须为
`0x48000`，同时用作 delta 解码工作页窗口的参数。

`FOTAL\0` 位于不同的 binary literal，用作 FAL 重启恢复记录的前 6 bytes，后续
2 bytes 为阶段字；它不是这个 `0x44` bytes 文件头的 magic。journal 轮转中
`0xfcfc` 表示新副本准备、`0xf0f0` 是唯一接受的稳定副本、`0xc0c0` 表示旧副本待擦；
其页内记录的阶段字节在代码中按 `0xfc -> 0xf0 -> 0xc0 -> 0x00` 写入。字段未命名
部分及 FAL 的外部进入条件仍未恢复，不能将此 normal-FAL 私有 journal 与 A/B 的
`ota_fina_executive` 状态扇区混为一谈。可复现证据见
[normal bootloader Ghidra 记录](../../../../docs/verification/bk7258/2026-08-07-ghidra-bk7258-normal-bootloader.md)。

---

## 3. 寄存器操作表

### 3.1 系统寄存器 (SYS_REG_BASE = 0x44010000)

| 偏移 | 寄存器 | 用途 | bootloader 操作 |
|------|--------|------|----------------|
| 0x08 | SYS_CTRL | 系统控制 | 配置 WDT/时钟门 |
| 0x10 | SYS_INT_STATUS | 中断状态 | 等待 flash 就绪 |
| 0x14 | SYS_CLK_CTRL | 时钟控制 | 配置 PLL 时钟源 |
| 0x20 | SYS_PERI_CLK_CTRL | 外设时钟门 | 使能 UART/SPI 时钟 |
| 0x24 | SYS_PERI_CLK_CTRL2 | 外设时钟门2 | flash 时钟配置 |
| 0x28 | SYS_CLK_DIV | 时钟分频 | 设置 flash/UART 分频 |
| 0x30 | SYS_CLK_SEL | 时钟选择 | PLL 时钟源选择 |
| 0x40 | SYS_FLASH_CTRL | Flash 控制 | 配置 flash 读模式 |
| 0x54 | SYS_FLASH_ADDR | Flash 地址 | 设置读取地址 |
| 0x104 | WDT_CTRL | WDT 控制 | 清除 WDT 标志 |
| 0x120 | SYS_CPU_BOOT | CPU 启动地址 | 多核启动配置 |

### 3.2 Flash 控制器 (0x44000000 区域)

| 偏移 | 寄存器 | 用途 | bootloader 操作 |
|------|--------|------|----------------|
| 0x08 | FLASH_CLK_CFG | Flash 时钟 | 设置时钟分频 |
| 0x10 | FLASH_STATUS | Flash 状态 | 忙等待循环 |
| 0x14 | FLASH_OP_MODE | 操作模式 | 设置读/写/擦除模式 |
| 0x18 | FLASH_DATA | Flash 数据 | 读取 32 字节数据 |
| 0x1C | FLASH_CMD | Flash 命令 | 发送读/写/擦除命令 |
| 0x20 | FLASH_ADDR | Flash 地址 | 设置目标地址 |
| 0x24 | FLASH_CONFIG | Flash 配置 | 设置读参数 |
| 0x28 | FLASH_CACHE_CTRL | Cache 控制 | 使能/禁用 cache |

### 3.3 AON PMU (0x44000000)

| 偏移 | 寄存器 | 用途 | bootloader 操作 |
|------|--------|------|----------------|
| 0x08 | AON_PMU_CTRL | PMU 控制 | 检查 bit16 (XIP 模式) |

### 3.4 WDT (0x44800000)

| 偏移 | 寄存器 | 用途 | bootloader 操作 |
|------|--------|------|----------------|
| 0x10 | WDT_UNLOCK | 解锁寄存器 | 写 0x005A0000 + 0x00A50000 |

### 3.5 SCB (0xE000ED00)

| 偏移 | 寄存器 | 用途 | bootloader 操作 |
|------|--------|------|----------------|
| 0x08 | SCB->VTOR | 向量表偏移 | 设置为 app 向量表地址 |
| 0x14 | SCB->SHCSR | 系统处理控制 | Cache 使能控制 |
| 0x80 | SCB->CCR | 配置控制 | Cache 区域配置 |
| 0x84 | SCB->CCR2 | 配置控制2 | 清零 |
| 0x274 | Cache Region | Cache 区域 | 设置 cache 起始/结束地址 |

---

## 4. 分区表机制

### 4.1 分区表位置

bootloader 使用**运行时查询**机制, 而非固定指针:

```
bootloader 不从固定偏移 (如涂鸦的 0x2980) 读取分区表
而是通过 partition_get_info(partition_id) 函数动态查询
分区表存储在 flash 的可配置区域, 由 fal (Flash Abstraction Layer) 管理
```

### 4.2 分区查询流程 (0x2003774)

```c
// 伪代码
partition_info_t* partition_get_info(int partition_id) {
    // 1. 从 flash 读取分区表头
    // 2. 遍历分区条目
    // 3. 匹配 partition_id
    // 4. 返回分区信息结构体
    //    struct {
    //        char name[16];      // +0x00
    //        uint32_t offset;    // +0x34: flash 偏移 (逻辑扇区号)
    //        uint32_t size;      // +0x38: 分区大小
    //        ...
    //    }
}
```

### 4.3 CRC 展开地址计算

flash 使用 CRC-expanded 格式, 每 32 字节逻辑数据对应 34 字节物理数据:

```c
// 物理地址 = 逻辑地址 * 34 / 32
// 逻辑地址 = 物理地址 * 32 / 34
// 实际代码 (0x20017D0):
uint32_t real_addr = (partition_offset * 32) / 34 + 0x02000000;
```

### 4.4 末尾分区配置数据 (0xCC00+)

0xCC00 区域是 **MPC (Memory Protection Controller) 配置表**, 定义内存区域访问权限:

```
偏移    数据                    含义
0xCC00: BB44 00FF AA00 0000    MPC 表头 (magic + version)
0xCC08: 0600 0000 E33F 0000    区域数量(6) + 总配置大小

区域 0 (0xCC10): Flash 区域
  类型=0x02, 起始=0x02000000, 结束=0x02FFFFE9

区域 1 (0xCC18): SRAM 区域
  类型=0x06, 起始=0x08000000, 结束=0x0809FFE3

区域 2 (0xCC20): DTCM 区域
  类型=0x03, 起始=0x20000000, 结束=0x20003FE3

区域 3 (0xCC28): SRAM0 区域
  类型=0x1A, 起始=0x28000000, 结束=0x3FFFFFE3

区域 4 (0xCC30): 外设区域
  类型=0x1B, 起始=0x40000000, 结束=0x5FFFFFE5

区域 5 (0xCC38): PSRAM/QSPI 区域
  类型=0x03, 起始=0x60000000, 结束=0x63FFFFE7
```

---

## 5. App Header 校验

### 5.1 App 向量表格式 (Flash XIP)

```
偏移      内容
0x000     MSP (栈顶指针, 必须指向有效 SRAM)
0x004     Reset_Handler (Thumb 地址, bit0=1)
0x008-0x0FF  异常/中断向量表
0x100     Magic "BK7236\0\0" (8 字节)
0x108-0x13F  扩展中断向量 (IRQ 48-63)
0x140+    App 代码
```

### 5.2 Magic 校验

```
偏移 0x100: 42 4B 37 32 33 36 00 00 = "BK7236" + 0x00 + 0x00
```

bootloader 通过 `strcmp` 或 `memcmp` 校验此 magic。注意:
- bootloader 自身的 magic 在 0x100: `BK7236\x10\x00` (0x10 标识 bootloader)
- app 的 magic 在 0x100: `BK7236\0\0` (双 null 标识 app)

### 5.3 MSP 范围检查

```c
// MSP 必须指向 BK7258 SRAM 范围
// SRAM0: 0x28000000 - 0x2800FFFF (64KB)
// SRAM1: 0x28010000 - 0x2801FFFF (64KB)
// SRAM2: 0x28020000 - 0x2803FFFF (128KB)
// SRAM3: 0x28040000 - 0x2805FFFF (128KB)
// SRAM4: 0x28060000 - 0x2807FFFF (128KB)
// SRAM5: 0x28080000 - 0x2809FFFF (128KB)
// 总计: 640KB, 顶部 = 0x2809FFFC
```

### 5.4 Reset_Handler Thumb 位

```c
// Reset_Handler 地址必须是奇数 (Thumb 指令)
// 读取 [app_base + 4] 得到 Reset_Handler
// 检查 bit0 == 1
// 如果 bit0 == 0, 说明是 ARM 模式, 对于 Cortex-M33 是非法的
```

---

## 6. CRC 校验

### 6.1 CRC 类型

bootloader 使用两种 CRC:

| CRC 类型 | 多项式 | 用途 | 位置 |
|----------|--------|------|------|
| CRC16 | 0x8320 | Flash CRC-expanded 读写 | 0x2001414 (表初始化) |
| CRC32 | 0xEDB88320 | OTA 头/体校验 | 0x2001EF4 (计算函数) |

### 6.2 CRC16 Flash 展开格式

```
物理 flash 布局 (每 34 字节一组):
┌──────────────────────────┬──────────┐
│ 32 字节逻辑数据          │ 2 字节   │
│                          │ CRC16    │
└──────────────────────────┴──────────┘

地址转换公式:
  physical_offset = logical_offset * 34 / 32
  logical_offset = physical_offset * 32 / 34
```

### 6.3 CRC32 表位置

```c
// CRC32 查找表 (256 项, 1024 字节)
// 存储在 SRAM: 0x28000574
// 多项式: 0xEDB88320 (标准 CRC32)
// 初始化: 0xFFFFFFFF
// 最终异或: ~result
```

### 6.4 OTA 校验流程

```
1. 读取 RBL 头 (96 字节)
2. 校验 magic `RBL\0` (偏移 `+0x00`)
3. 计算前92 bytes header CRC32，与`+0x5c`比较
4. normal RBL从`+0x60`读取stored payload；AB容器的payload从partition开头读取，header位于
   logical容器结束前4 KiB
5. 计算stored-body CRC32，与`+0x4c`比较
6. algorithm 0时raw body等于stored body，按`+0x54`长度计算FNV-1a并与`+0x50`比较
7. encoded算法需要先解密/解压再验证raw hash；不能把FNV误称SHA-256
8. 任一校验失败 → fail-closed；全部通过只证明完整性，不证明签名真实性
```

---

## 7. 多核唤醒

### 7.1 结论: bootloader 不唤醒 CPU1/CPU2

bootloader 代码中**没有**多核唤醒逻辑。CPU1/CPU2 的启动由 app 层负责:

```c
// app 层代码 (system_main.c):
void start_cpu1_core(void) {
    uint32 addr = get_partition_addr(1);  // 获取 CPU1 分区地址
    reset_cpu1_core(addr, 1);             // 设置启动地址并释放复位
}

void reset_cpu1_core(uint32 offset, uint32_t start_flag) {
    sys_drv_set_cpu1_pwr_dw(0);           // 释放 CPU1 下电
    sys_drv_set_cpu1_rxevt_sel(1);        // 设置事件选择
    sys_drv_set_cpu1_boot_address_offset(offset >> 8); // 启动地址
    sys_drv_set_cpu1_reset(start_flag);   // 释放/保持复位
}
```

### 7.2 多核启动寄存器

```c
// CPU1 启动地址配置 (SYS_REG 区域):
// sys_drv_set_cpu1_boot_address_offset(offset >> 8)
// 写入 SYS_REG + 某偏移, 设置 CPU1 从 flash XIP 地址启动

// CPU1 复位控制:
// sys_drv_set_cpu1_reset(1) → 释放复位, CPU1 开始执行
// sys_drv_set_cpu1_reset(0) → 保持复位, CPU1 停止
```

---

## 8. 对"自己写 bootloader"的可复用结论

### 8.1 最小 bootloader 必须做的事

```
1. 向量表 (0x000-0x0FF)
   - SP = 0x28030000 (或更高, 取决于 app 需求)
   - Reset_Handler = 你的入口地址

2. Magic (0x100)
   - "BK7236\x10\x00" (8 字节)
   - BootROM 校验此 magic, 必须精确匹配

3. 硬件初始化
   - 时钟: 配置 PLL, 等待锁定
   - UART: 配置 GPIO pinmux + 波特率 (可选, 用于调试)
   - Flash: 配置读模式 (single/quad)
   - WDT: 解锁并配置 (或禁用)

4. App 加载
   - 读取 app 向量表 (从 flash 分区)
   - 校验 magic "BK7236\0\0" (偏移 0x100)
   - 校验 MSP 范围 (0x28000000 - 0x2809FFFC)
   - 校验 Reset_Handler Thumb 位 (bit0 = 1)

5. 跳转
   - SCB->VTOR = app_vector_table_addr
   - MSP = app_MSP
   - 清空 r0-r12
   - dsb + isb
   - bx Reset_Handler
```

### 8.2 关键寄存器清单

```c
// 系统时钟
#define SYS_REG_BASE      0x44010000
#define SYS_CLK_CTRL      (SYS_REG_BASE + 0x08)
#define SYS_CLK_SEL       (SYS_REG_BASE + 0x30)
#define SYS_FLASH_CTRL    (SYS_REG_BASE + 0x40)

// Flash 控制器
#define FLASH_CTRL_BASE   0x44000000
#define FLASH_STATUS      (FLASH_CTRL_BASE + 0x10)
#define FLASH_DATA        (FLASH_CTRL_BASE + 0x18)
#define FLASH_CMD         (FLASH_CTRL_BASE + 0x1C)

// WDT
#define WDT_BASE          0x44800000
#define WDT_UNLOCK        (WDT_BASE + 0x10)

// SCB (Cortex-M33)
#define SCB_BASE          0xE000ED00
#define SCB_VTOR          (SCB_BASE + 0x08)
```

### 8.3 Flash CRC-expanded 读写

```c
// 如果使用 CRC-expanded flash (每 32 字节 + 2 字节 CRC16):
// 读取时需要先读 34 字节, 验证 CRC16, 提取 32 字节数据
// 写入时需要计算 CRC16, 拼接成 34 字节写入

// 如果不使用 CRC-expanded (直接 XIP):
// 直接从 flash 读取, 无需 CRC 处理
// 地址转换: real_addr = offset + 0x02000000
```

### 8.4 App 镜像格式

```
偏移      内容                    必需?
0x000     MSP (栈顶)              是
0x004     Reset_Handler           是
0x008-0x0FF  向量表               是
0x100     "BK7236\0\0" magic     是 (bootloader 校验)
0x108+    扩展向量 / 代码         是
```

### 8.5 调试建议

```
1. 先用 UART 输出调试信息 (115200, 8N1)
2. 打印 flash ID 确认 flash 可读
3. 打印分区地址确认分区表正确
4. 打印 app MSP 和 Reset_Handler 确认跳转目标
5. 跳转前打印 "jump to 0x%x" 确认即将跳转
```

### 8.6 已知陷阱

```
1. Bootloader magic 必须是 "BK7236\x10\x00", 不是 "BK7236\0\0"
2. 向量表偏移 0x100 处必须是 magic, 不能是代码
3. MSP 必须指向有效 SRAM, 否则 HardFault
4. Reset_Handler 必须是 Thumb 地址 (bit0=1)
5. 跳转前必须 DSB + ISB, 否则 cache 可能导致问题
6. 跳转前必须清空 VTOR, 否则中断会跳回 bootloader
7. CRC-expanded flash 的地址转换不能用简单乘除, 要用 sdiv
8. WDT 解锁需要连续写两个 magic 值, 顺序不能错
```

---

## 附录 A: 字符串表

| 文件偏移 | 字符串 | 用途 |
|----------|--------|------|
| 0x6B95 | " flash id:" | 打印 flash ID |
| 0x6CE7 | " u_bootloader enter" | 启动信息 |
| 0x6CFD | " handler ret:" | 返回值打印 |
| 0x6D0B | "RET_DM_FAILURE!" | 下载模式失败 |
| 0x6D1D | "dl part NO_OTA_DATA!" | 无 OTA 数据 |
| 0x6D34 | "dl part NO_RBL_HEAD or no app1!" | 无 RBL 头 |
| 0x6D56 | "dl part over!" | 下载完成 |
| 0x6D66 | "img hash err!" | 镜像哈希错误 |
| 0x6D8C | "beken_onchip_crc" | 片上 CRC 设备名 |
| 0x6DF6 | " jump toxx:0x%x" | 跳转信息 |
| 0x6E0A | "device_table[i]->ops.read" | 设备读断言 |
| 0x6E38 | "device_table[i]->ops.write" | 设备写断言 |
| 0x6E53 | "device_table[i]->ops.erase" | 设备擦除断言 |
| 0x6E7B | "download" | 分区名 |
| 0x6E84 | "fal_flash_device_find" | flash 设备查找 |
| 0x6E9A | "fal_flash_init" | flash 初始化 |
| 0x6F09 | "Verify firmware CRC32 failed on partition " | CRC 校验失败 |
| 0x6F35 | "Verify ota body partition success." | OTA 校验成功 |
| 0x6FB2 | "Verify firmware hash(calc.hash: %08lx != hdr.hash: %08lx) failed on partition '%s'." | 哈希校验失败 |
| 0x7030 | " inflate len:" | 解压长度 |
| 0x7040 | " inflate ret:" | 解压结果 |
| 0x704E | "CPU_OPREATE_FLASH over!" | Flash 操作完成 |
| 0x706A | "hash sucess" | 哈希成功 |
| 0x7078 | "hash fail" | 哈希失败 |
| 0x70E3 | "not diff-fota header!" | 非差分 OTA 头 |
| 0x70FB | "ota header crc error %x %x!" | OTA 头 CRC 错误 |
| 0x7119 | "compress parameter not match, %x %x!" | 压缩参数不匹配 |
| 0x7140 | "crc error, %x %x!" | CRC 错误 |
| 0x7154 | "ota file crc error!" | OTA 文件 CRC 错误 |
| 0x716A | "no update partition!" | 无更新分区 |
| 0x7181 | "new version overflows target partition!" | 新版本溢出 |
| 0x727A | "file version not match!" | 版本不匹配 |
| 0x7310 | " head CRC32 fail." | 头 CRC 失败 |
| 0x7322 | "head crc sucess" | 头 CRC 成功 |
| 0x7334 | "body CRC32 failed." | 体 CRC 失败 |
| 0x7347 | "aes_decrypt_inflate_handler" | AES 解密+解压 |
| 0x7363 | "fal_partition_read_hash" | 分区哈希读取 |
| 0x737B | "ota_body_fw_verify" | OTA 体固件校验 |
| 0x738E | "dm_erase_dest_partition" | 擦除目标分区 |

## 附录 B: Flash ID 表 (0x6B90+)

bootloader 内嵌 JEDEC flash ID 表, 每条目 16 字节:

```
偏移    制造商ID  设备ID    容量shift  类型
0x6BA0: 0x1670    0x1C00    0x01       0x1F
0x6BB0: 0x1570    0x1C00    0x01       0x1F
0x6BC0: 0x1440    0x0B00    0x02       0x1F
0x6BD0: 0x1540    0x0B00    0x02       0x1F
0x6BE0: 0x1640    0x0B00    0x02       0x1F
0x6BF0: 0x1640    0x0E00    0x02       0x1F
0x6C00: 0x1640    0x2000    0x02       0x1F
... (约 30 种 flash 型号)
```

## 附录 C: OTA 功能概述

bootloader 包含完整的 OTA (Over-The-Air) 更新能力:

```
支持模式:
├─ 全量更新 (full OTA)
├─ 差分更新 (diff-fota)
├─ AES 解密 (aes_decrypt_inflate_handler)
├─ LZ 压缩解压 (inflate)
└─ v3.1.1.9 RBL CRC32 + 32-bit FNV-1a 完整性校验

OTA 流程:
1. 检测 OTA 标志 (download 分区)
2. 读取 OTA 头, 校验 CRC32
3. 读取 OTA 体，校验stored-body CRC32，并对raw body校验FNV-1a
4. 擦除目标 app 分区
5. 解压 (如果压缩) 并写入目标分区
6. 清除 OTA 标志
7. 重启, 正常启动新 app
```
