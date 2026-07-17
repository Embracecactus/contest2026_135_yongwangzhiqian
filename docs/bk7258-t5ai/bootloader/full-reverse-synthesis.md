# BK7258 Bootloader 完整逆向综合（涂鸦 + BK 官方）

> 基于 `tuya-bootloader-reverse.md`（761 行）和 `bk-official-bootloader-reverse.md`（940 行）的综合。
> 用 `hardware-review-gate` 二进制模式 + SDK 源码交叉引用完成。

## 一、两者共享的核心机制（写自己 bootloader 必须复用）

### 1. BootROM → bootloader 契约

| 项 | 值 | 说明 |
|---|---|---|
| Bootloader 链接基址 | `0x02000000` | BootROM 跳转目标 |
| Boot magic 偏移 | logical `0x100` / physical `0x110` | CRC 扩展后 32→34 字节 |
| Boot magic 值 | `BK7236\x10\x00`（8字节） | BootROM 校验此标识 |
| 初始 MSP | `0x28030000` | SRAM 中部 |
| Reset_Handler | `0x020001C1`（Thumb） | 两者一致 |

### 2. Flash 镜像格式（CRC 硬件处理）

```
物理 flash:  [32字节数据][2字节CRC16][32字节数据][2字节CRC16]...
逻辑地址:    CPU 看到的连续 32 字节块（flash 控制器透明解码）
转换公式:    physical = (logical / 32) * 34 + (logical % 32)
```

- CRC16 多项式：`0x8005`（big-endian 追加）
- **CRC 由 flash 控制器硬件处理**，bootloader/app 不做软件解码
- bootloader 自己的镜像也按此格式存储

### 3. App header 校验 + 跳转（主路径）

```
1. 从 app 基址 0x02010000 读向量表
   [0x000] = app MSP  → 校验 0x28000000 <= MSP <= 0x280A0000
   [0x004] = app Reset → 校验 bit0=1 (Thumb)
2. 校验 app magic @ 0x02010100: "BK7236\0\0"
3. 设 VTOR = 0x02010000 (SCB @ 0xE000ED08)
4. 设 MSP = app MSP
5. 清寄存器 r0-r12, DSB, ISB
6. bx app Reset_Handler
```

### 4. 多核唤醒——**bootloader 不负责**

CPU1/CPU2 的启动由 **app 层**处理（`system_main.c` 的 `start_cpu1_core()`）：
- `sys_drv_set_cpu1_boot_address_offset(offset >> 8)`
- `sys_drv_set_cpu1_reset(start_flag)`

→ 自己的 bootloader **不需要**做多核唤醒，单核 bootloader 跳到 app 后由 app 决定是否唤醒从核。

## 二、两者差异（涂鸦扩展了什么）

| 项 | BK 官方 | 涂鸦 | 影响 |
|---|---|---|---|
| 大小 | 52KB | 65KB（满 64KB） | 涂鸦多了 OTA 引擎 |
| Boot magic 位置 | logical 0x100 | logical 0x100（0x100-0x10F 有分区指针，magic 仍在 0x110 物理即 0x100 逻辑） | **一致** |
| 分区表 | 末尾 MPC 配置（0xCC00+） | 内嵌 FAL 分区表（0x81C0+） | 涂鸦用 FAL，官方用固定布局 |
| OTA | 无 | diff2ya/bspatch 增量 OTA | 涂鸦独有 |
| CRC 校验点 | flash 控制器硬件 | flash 控制器硬件 + OTA CRC32 | OTA 相关 |
| WDT 解锁 | `0x5A`/`0xA5` key | 同 | 一致 |
| Flash 控制器基址 | `0x44000000` | `0x44000000` | 一致 |

**结论**：两者核心启动逻辑一致，涂鸦额外加了 OTA + FAL 分区。对于"只跳 NuttX"的 bootloader，两者都只是 header 校验 + 跳转。

## 三、对自己 bootloader 的可复用结论

### 必须实现的（最小集）

```c
reset_handler:
  1. cpsid i                          // 关中断
  2. 喂 WDT (0x44000600/0x44800010, key 0x5A/0xA5)
  3. (可选) 初始化 UART1 调试串口
  4. 读 app 向量表 @ 0x02010000:
     - MSP 范围校验 0x28000000..0x280A0000
     - Reset Thumb 位校验
  5. 校验 app magic @ 0x02010100: "BK7236\0\0"
  6. 设 VTOR = 0x02010000
  7. 设 MSP = app MSP
  8. bx app Reset_Handler
```

### 不需要实现的

- ❌ CRC 解码/校验（flash 控制器硬件处理）
- ❌ 多核唤醒（app 层负责）
- ❌ 分区表解析（固定偏移 0x02010000）
- ❌ OTA（NuttX 不需要）

### 已验证

你已有的 `bk7236_min_bl.S` **完全符合上述最小集**，Zephyr 已验证可跳转。无需修改 bootloader 逻辑，只需适配 NuttX 的向量表格式。

## 四、NuttX 适配要点

NuttX 编译产物 `nuttx.bin` 需要：

1. **链接地址 `0x02010000`**（linker script）
2. **向量表 `[0x000]`=MSP**（`0x2809FFFC`，SRAM 顶）
3. **`[0x004]`=Reset_Handler**（Thumb）
4. **`[0x100]`=magic `"BK7236\0\0"`**（8字节，由链接脚本或启动代码放置）
5. **CRC 扩展打包**（编译后用 `bk7258_crc_expand_app.py`，32+2 格式）
6. **烧录到 physical 0x11000**（对应 logical 0x02010000）
