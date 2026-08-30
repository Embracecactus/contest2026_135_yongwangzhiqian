# BK7258 CP/AP 双 NuttX 与 RPTUN/RPMsg 架构探索总结

> 日期：2026-07-26
> 状态：architecture research + N9 implementation closure；RPTUN/RPMsg wrapper 已 `board-verified`
> 权威工作树：`/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian`
> 外部参考：`/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9`、`/home/lijian/project/armino/vendor_beken`

## 1. 目标与边界

本轮探索为 BK7258/T5-AI 后续 CP/AP 全面适配确定架构边界。最终目标固定为：

```text
物理 CPU0 / CP:
  team-owned NuttX UP

物理 CPU1 + CPU2 / AP:
  一个独立的 team-owned NuttX SMP 镜像

CP/AP:
  mailbox doorbell + shared memory
  NuttX RPTUN/OpenAMP/RPMsg
```

约束如下：

1. 不把 CPU0、CPU1、CPU2 合并成一个三核 NuttX SMP 实例。
2. 最终运行时不依赖官方 `cp.bin` 或 `ap.bin`。
3. 允许通过 wrapper 使用官方 SDK/HAL、寄存器定义和必要的静态库，方式可参考 `vendor_beken`。
4. Wi-Fi 与 BLE 需要完整纳入最终架构，不能停留在简单 init 或演示接口。
5. 团队代码继续位于 contest overlay，不直接修改官方 NuttX checkout。
6. 当前 CPU0 NuttX UP baseline、时钟、IRQ、WDT、Flash/MTD/LittleFS、GPIO 等既有板端结论保持不变。

这里必须区分两种“独立”：

- **镜像独立**：CP/AP 都是团队构建的 NuttX 镜像，不运行官方 FreeRTOS CP/AP 固件。该目标可实现。
- **无线实现完全源码独立**：不使用任何 Beken Wi-Fi/BLE/PHY/controller 静态库或固件。目前材料不足以证明可实现，仍需确认许可与源码可用性。

---

## 2. 调查范围

本轮只读调查覆盖：

1. `/home/lijian/project/armino/vendor_beken`
   - Beken SDK/HAL 如何被包装进 NuttX。
   - OSAL、IRQ、lower-half、netdev、构建和打包边界。
   - Wi-Fi/BLE 的源码与预编译库边界。
2. `/home/lijian/project/armino/bk_avdk_smp`
   - CP/AP 镜像、CPU1/CPU2 启动链。
   - mailbox、`mb_chnl`、`mb_ipc`、virtual UART、日志、心跳。
   - Wi-Fi/BLE/Flash/PM 等服务 owner。
   - SRAM、PSRAM、cache、spinlock 约束。
3. 当前 NuttX/openvela tree
   - RPTUN、OpenAMP remoteproc、virtio、RPMsg。
   - RPMsg syslog、UART、FS、MTD、block、network、Bluetooth HCI 等现成服务。
   - NRF53、STM32H7、MPFS、K230 等 RPTUN lower-half 参考。

本轮没有修改代码、没有构建、没有烧录，也没有产生新的板端验证结论。

---

## 3. `vendor_beken` 的真实适配模式

### 3.1 结论

`vendor_beken` 不是“重写 Beken SDK”，也不是 CP/AP IPC 示例。它采用的是：

```text
Beken/Armino 预编译头文件和静态库
                +
NuttX OSAL / IRQ compatibility layer
                +
NuttX lower-half / netdev wrapper
                +
openvela board/chip build integration
```

它以 BK7236N 为目标，主要价值是提供“厂商 SDK/HAL 如何进入 NuttX”的参考模式，而不是直接提供 BK7258 CP/AP 实现。N9 已按这个官方 wrapper 边界落地：SDK 和 NuttX
保持只读，BK7258 特有的 mailbox/shared-memory/RPTUN 适配全部放在 contest board overlay。

### 3.2 构建注入

板级 defconfig 通过 custom dir 接入 vendor board/chip：

- `/home/lijian/project/armino/vendor_beken/boards/bk7236n/bk7236n-evb/configs/nsh/defconfig:13-20`

主要配置为：

```text
CONFIG_ARCH_CHIP_CUSTOM_DIR="../vendor/beken/chips/bk7236n"
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/beken/boards/bk7236n/bk7236n-evb"
```

构建系统完成以下工作：

1. 加入 `armino_as_lib` 头文件路径。
2. 编译 NuttX-facing glue source。
3. 定义 Armino 代码预期的 `CONFIG_*`，同时关闭 FreeRTOS。
4. 将 glue 直接链接进 `nuttx`，以强符号覆盖预编译库的弱 stub。
5. 链接 Beken 静态库并执行 CRC/bootloader 打包。

关键位置：

- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/CMakeLists.txt:21-74`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/CMakeLists.txt:76-176`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/Make.defs:31-129`
- `/home/lijian/project/armino/vendor_beken/boards/bk7236n/bk7236n-evb/CMakeLists.txt:23-49`

### 3.3 OSAL

`beken_os_adapt.c` 把 Armino RTOS API 映射到 NuttX：

| 厂商侧需求 | NuttX 实现 |
|---|---|
| 临界区 | `enter_critical_section()` / `leave_critical_section()` |
| 线程 | `kthread_create()` / `task_delete()` |
| 信号量 | `nxsem_*()` |
| Mutex | `nxmutex_*()` |
| 队列 | `file_mq_*()` |
| 软件定时器 | `wd_start()` / `wd_cancel()` |
| Heap | `kmm_malloc()` / `kmm_free()` / `kmm_realloc()` |
| Sleep | `nxsig_usleep()` |
| ISR 上下文 | `up_interrupt_context()` |

证据：

- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c:160-227`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c:273-383`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c:421-899`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c:990-1525`

该模式可直接指导 BK7258 的 SDK source/library adaptation，但 BK7258 需要自己的 OSAL 审计，不能直接使用 BK7236N 二进制。

### 3.4 IRQ wrapper

厂商库调用 `bk_int_isr_register()`，适配层将 Beken source 转成 NuttX IRQ，再调用：

```c
irq_attach();
up_enable_irq();
up_disable_irq();
irq_detach();
```

证据：

- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_interrupt_base.c:54-93`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_interrupt_base.c:111-183`

这与当前 BK7258 CPU0 已完成的 SDK IRQ bridge 属于同一种适配模式。

### 3.5 NuttX lower-half

`vendor_beken` 已将下列 Beken HAL/API 包装为 NuttX 标准模型：

- UART：`beken_uart.c`
- I2C：`beken_i2c_lowerhalf.c`
- SPI：`beken_spi.c`
- timer：`beken_tim_lowerhalf.c`
- oneshot：`beken_oneshot_lowerhalf.c`
- watchdog：`beken_wdt_lowerhalf.c`
- ADC：`beken_adc_lowerhalf.c`
- RTC：`beken_rtc_lowerhalf.c`
- PWM：`beken_pwm_lowerhalf.c`
- MTD：`beken_flash.c`
- RNG：`beken_trng_lowerhalf.c`

例如 I2C lower-half 最终调用 `bk_i2c_master_read/write()`：

- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_i2c_lowerhalf.c:66-89`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_i2c_lowerhalf.c:120-177`

因此 BK7258 后续单个外设应继续遵循：

```text
NuttX upper-half
      ↓
team lower-half wrapper
      ↓
Beken HAL/source
      ↓
hardware
```

### 3.6 Wi-Fi 与 BLE 的边界

Wi-Fi 已真正接入 NuttX netdev：

- `struct netdev_ops_s`
- `struct wireless_ops_s`
- `netdev_lower_register(..., NET_LL_IEEE80211)`
- interface name `wlan0`

证据：

- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_wlan.c:97-123`
- `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_wlan.c:1124-1143`

数据路径为：

```text
NuttX netpkt -> bk_wifi_send_tx_eth()
bk_wifi_get_rx_buffer() -> NuttX netpkt
```

- TX：`beken_wlan.c:219-243`
- RX：`beken_wlan.c:284-304`
- lower-half 通知：`beken_wlan.c:245-268`

BLE 侧则只看到 `bk_bluetooth_init()` 和厂商 API，没有完整 NuttX HCI/host integration：

- `/home/lijian/project/armino/vendor_beken/boards/bk7236n/bk7236n-evb/src/beken_ap.c:176-178`

### 3.7 预编译依赖

`vendor_beken` 的无线核心大量位于预编译库：

```text
libbk_wifi.a
libwifi.a
libwpa_supplicant-2.10.a
liblwip_intf_v2_1.a
libbk_netif.a
libbk_bluetooth.a
libbluetooth_controller_controller_only.a
libbk_coex.a
libbk_phy.a
libbk_phy_info.a
libcom_phy.a
```

构建会链接目录内全部 `.a`：

- `/home/lijian/project/armino/vendor_beken/boards/bk7236n/bk7236n-evb/CMakeLists.txt:23-30`
- `/home/lijian/project/armino/vendor_beken/boards/bk7236n/bk7236n-evb/scripts/Make.defs:40-49`

所以 `vendor_beken` 应被描述为：

> 有实质 NuttX glue 的二进制 SDK 集成，而不是完整源码无线移植。

---

## 4. 官方 BK7258 CP/AP 镜像与启动模型

### 4.1 镜像关系

官方构建输出只有两个应用执行镜像：

```text
CP -> app.bin
AP -> app1.bin
```

AP 的 CPU1 和 CPU2 共用一个 AP image，不存在独立 CPU2 flash image。

AP linker script 将多套向量表打进同一个 image：

- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/soc/bk7258_ap/bk7258_ap_bsp.ld:102-122`

当前 AP 配置是两核 SMP，因此实际使用：

```text
__vector_core0_table
__vector_core1_table
```

`__vector_core2_table` 虽然被链接进 image，但不属于当前 CPU1+CPU2 两核 AP 的主要启动链。

### 4.2 准确的物理核/逻辑核映射

| AP 本地逻辑核 | 物理核 | AP 启动入口 |
|---|---|---|
| logical core 0 | CPU1 | `Reset_Handler_Cpu0` / `__vector_core0_table` |
| logical core 1 | CPU2 | `Reset_Handler_Cpu1` / `__vector_core1_table` |

全 SoC physical ID 为 0/1/2：

- `/home/lijian/project/armino/bk_avdk_smp/ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Include/cpu_id.h:17-22`

AP API 层对本地 core ID 加 1，因此对外返回 physical 1/2：

- `/home/lijian/project/armino/bk_avdk_smp/ap/include/os/os.h:1281-1292`

后续 NuttX SMP 必须显式区分：

```text
NuttX local CPU index: 0 / 1
BK physical CPU ID:    1 / 2
```

### 4.3 CPU0 启动 AP primary

CP 只从 `BK_PARTITION_APPLICATION1` 获取 AP image：

- `/home/lijian/project/armino/bk_avdk_smp/cp/components/bk_startup/system_main.c:142-171`

低层启动序列：

```c
sys_drv_set_cpu1_pwr_dw(0);
sys_drv_set_cpu1_rxevt_sel(1);
sys_drv_set_cpu1_boot_address_offset(offset >> 8);
sys_drv_set_cpu1_reset(1);
```

- `/home/lijian/project/armino/bk_avdk_smp/cp/components/bk_startup/system_main.c:173-195`

官方路径还对 CRC-packed flash 地址执行 34/32 换算，并叠加 `SOC_FLASH_DATA_BASE`。团队 dual-image packer 必须统一“physical packed address”和“CPU-visible logical/XIP address”的定义，不能直接复制常量。

### 4.4 AP primary 启动 AP secondary

物理 CPU1/AP logical core0 在 SMP scheduler bring-up 中调用：

```c
multicore_launch_core1(...);
```

该函数把物理 CPU2 指向同一 AP image 的 `__vector_core1_table`：

```c
reset_cpu2_core((uint32_t)&__vector_core1_table, 1);
```

证据：

- `/home/lijian/project/armino/bk_avdk_smp/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/portable/GCC/ARM_CM33_NTZ/non_secure/port.c:1202-1223`
- `/home/lijian/project/armino/bk_avdk_smp/ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c:259-267`
- `/home/lijian/project/armino/bk_avdk_smp/ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c:296-318`

因此：

1. CPU0 只负责启动 AP primary/物理 CPU1。
2. CPU0 不应直接代管 AP secondary/物理 CPU2。
3. AP NuttX logical core0 应负责启动 logical core1。
4. AP SMP startup 的主要参考必须来自 `bk_avdk_smp/ap`，不是 CP tree 中同名的 startup 文件。

### 4.5 AP boot-ready

释放 CPU1 不等于 AP service 已可用。官方 AP 在 `bk_init()` 完成后发送 `PM_CPU1_BOOT_READY_CMD`：

- AP 发 ready：`/home/lijian/project/armino/bk_avdk_smp/ap/components/bk_init/bk_init.c:343-347`
- AP mailbox send：`/home/lijian/project/armino/bk_avdk_smp/ap/middleware/driver/pwr_clk/pwr_clk.c:132-139`
- CP 接收：`/home/lijian/project/armino/bk_avdk_smp/cp/middleware/driver/pwr_clk/pwr_clk.c:226-233`

团队实现也必须保留等价状态机：

```text
CPU1 released
  -> AP primary initialized
  -> AP secondary online
  -> RPTUN callback registered
  -> RPMsg nameservice ready
  -> AP_READY
```

### 4.6 Secure 状态

官方 AP 配置为 secure-only，而不是独立 Non-Secure application：

- `CONFIG_SPE=1`：`ap/middleware/soc/bk7258_ap/bk7258_ap.defconfig:41`
- `configRUN_FREERTOS_SECURE_ONLY=1`：`ap/components/bk_rtos/freertos/FreeRTOSConfig.h:250-257`

AP NuttX 的第一版建议继续在 Secure state bring-up，避免在 SMP、mailbox、共享内存之外同时引入 TrustZone split。

---

## 5. 官方 service ownership

### 5.1 CP owner

CPU0/CP 明确负责或作为 server/backend：

- AP boot/reset/power。
- WDT 和系统级恢复。
- Flash controller/shared lock。
- PHY/RF/coexistence。
- Wi-Fi controller/backend。
- BLE controller。
- 多项 PM/DVFS 服务。
- AP heartbeat supervision。

Flash/SARADC/PHY 的 server/client ID 明确编码在：

- `/home/lijian/project/armino/bk_avdk_smp/cp/include/driver/mb_ipc_port_cfg.h:34-59`

### 5.2 AP owner

AP 主要承载：

- 应用业务。
- multimedia/media service。
- camera/display/audio/video pipeline。
- Wi-Fi frontend 和应用网络面。
- Bluetooth host。
- 大部分用户可见服务。

示例普遍执行：

```c
bk_init();
media_service_init();
```

证据：

- `/home/lijian/project/armino/bk_avdk_smp/projects/asr_service_example/ap/ap_main.c:7-14`
- `/home/lijian/project/armino/bk_avdk_smp/projects/audio_player_example/ap/ap_main.c:7-14`
- `/home/lijian/project/armino/bk_avdk_smp/projects/video_pipeline_example/ap/ap_main.c:36-46`

### 5.3 BLE 分层

官方 BLE 分层非常明确：

```text
CP: Controller-only
AP: Host-only
```

证据：

- AP：`projects/app/ap/config/bk7258_ap/config:137-148`
- CP：`projects/app/cp/config/bk7258/config:121-140`
- AP BT channel：`ap/components/bk_bluetooth/ipc/src/bt_ipc_core.c:33-36`

这与 NuttX `bt_rpmsghci` 的 controller/host split 高度匹配。

### 5.4 Wi-Fi 分层

官方 Wi-Fi 不是“CP 独占整个网络栈”，而是：

```text
CP:
  Wi-Fi controller/backend + PHY/RF

AP:
  Wi-Fi frontend + network/application surface
```

证据：

- CP 打开真正的 Wi-Fi/controller 配置：`projects/app/cp/config/bk7258/config:360-420`
- AP 未打开完整 `CONFIG_WIFI_ENABLE`，但仍包含 Wi-Fi driver/netif/lwIP：`projects/app/ap/config/bk7258_ap/config:583-635`
- AP command/data channel：`ap/components/bk_wifi_driver/wdrv_ipc.h:17-18`
- CP controller channel：`cp/components/controller_if/cif_ipc.h:17-18`

AP 和 CP 都存在 lwIP 组件，所以官方低功耗网络行为仍需进一步专项审计；不能简单宣称 TCP/IP 完全只属于某一侧。

团队最终更应采用 vendor-aligned 模型：

```text
CP NuttX:
  Wi-Fi controller/PHY/RF

AP NuttX:
  Wi-Fi netdev frontend + NuttX network stack + POSIX sockets
```

`usrsock_rpmsg` 可作为阶段性简化方案，但不作为最终首选架构。

---

## 6. 官方 mailbox/IPC 分层

官方层次为：

```text
mailbox register / FIFO / IRQ
            ↓
mbox0_hal / mbox0_drv
            ↓
mb_chnl logical channels
            ↓
mb_ipc / mb_uart / bk_api_ipc / Wi-Fi IPC / BT IPC / log
```

### 6.1 可复用的硬件层

适合用于 BK7258 RPTUN lower-half 的部分：

- `mbox0_hal.*`：FIFO、status、send/recv、IRQ clear。
- `mbox0_drv.c`：ISR drain 和 CPU endpoint routing。
- `mailbox_driver.c`：系统级中断 gate。
- CPU boot/reset/power helper。
- cache maintenance 和共享内存约束。

关键文件：

- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/soc/common/hal/include/mbox0_hal.h`
- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/soc/common/hal/mbox0_hal.c`
- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/driver/mailbox/mbox0_drv.c`

### 6.2 `mb_chnl`

`mb_chnl` 提供：

- logical channel ID。
- RX/TX/TX-complete ISR。
- synchronous write。
- shared exchange buffer。

- channel enum：`cp/include/driver/mailbox_channel.h:64-97`
- API：`cp/middleware/driver/mailbox/mailbox_channel.c:667-709,777-869`

它可作为硬件时序和 ACK 语义参考，但 RPTUN 不需要完整复制所有 logical channel ABI。

### 6.3 `mb_ipc` 不应成为 RPTUN substrate

`mb_ipc` 已经是一套高于 HAL 的协议，包含：

- socket/port/route。
- connect/disconnect/send/recv。
- CRC/tag/user command。
- semaphore/retry。
- 静态 route/socket 表。
- 裸共享指针和生命周期约束。

证据：

- `/home/lijian/project/armino/bk_avdk_smp/ap/include/driver/mb_ipc.h:27-101`
- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/driver/mailbox/mb_ipc.c:68-177`
- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/driver/mailbox/mb_ipc.c:1761-1766`
- `/home/lijian/project/armino/bk_avdk_smp/ap/middleware/driver/mailbox/mb_ipc.c:1971-1985`

因此正式架构应为：

```text
mbox0 HAL/IRQ
      ↓
BK7258 RPTUN lower-half
      ↓
OpenAMP/RPMsg
```

而不是：

```text
mbox0 -> mb_chnl -> mb_ipc -> RPTUN -> RPMsg
```

### 6.4 virtual UART 与日志

所谓 virtual UART 实际是直接建立在 `mb_chnl` 上的 mailbox UART driver，不经过 `mb_ipc`：

- `MB_CHNL_UART0/1`：`cp/middleware/driver/mailbox/mb_uart_driver.c:756-774`
- TX 使用 `mb_chnl_write()`：`cp/middleware/driver/mailbox/mb_uart_driver.c:534-597`

AP log/remote CLI 同样直接使用 `MB_CHNL_LOG`：

- AP log channel：`ap/components/bk_cli/shell_mailbox_ipc.c:40-49`
- AP log forwarding：`ap/components/bk_cli/shell_task.c:2321-2439`

双 NuttX 最终实现应优先使用 `syslog_rpmsg`，仅在需要真实 TTY 语义时使用 `uart_rpmsg`。

### 6.5 AP SMP mailbox 冲突

官方 AP mailbox driver 会把 `mbox0_message_t.data[1] == 0` 的消息解释为 SMP
cross-core command：

- `ap/middleware/driver/mailbox/mbox0_drv.c:59-70`
- `ap/middleware/driver/mailbox/mbox0_cross_core.c:34-45`

v3.1.1.9 还明确给出：

```text
MBOX0 base       0x41000000
CPU channels     0 / 1 / 2
total FIFO       8 entries
SMP FIFO split   2 / 3 / 3
```

当前团队 N7 lifecycle raw-register path 与 N8 SDK FIFO path 都访问该 MBOX0
控制器。因此 N9 不只是“另分一个逻辑 channel”，还必须先解决同一硬件被两套
初始化/寄存器 ABI 同时拥有的问题。

所以 AP NuttX 必须：

1. 将 CPU1↔CPU2 SMP IPI 与 CPU0↔CPU1 RPTUN notify 分离。
2. 冻结唯一的 MBOX0 init/FIFO/IRQ owner，将 lifecycle 迁移到统一 FIFO ABI，或
   定义严格的 pre-FIFO handoff。
3. 永久保留 `data[1] == 0` 给 SMP IPI；CP/AP control/RPTUN 使用非零 type tag。
4. 在共享 control header 中保存完整 generation/pending bit，mailbox 只作 edge。
5. 定义 FIFO full、coalescing、重复 kick 和 ISR drain policy。
6. 将 CP/AP mailbox IRQ affinity 固定到 AP logical core0/physical CPU1。
7. 不让 CPU2 成为第二个 RPMsg peer。

---

## 7. NuttX RPTUN/OpenAMP/RPMsg 结论

### 7.1 层次

NuttX 已提供完整通用 IPC 框架：

```text
SoC mailbox/shared memory lower-half
                  ↓
                RPTUN
                  ↓
        OpenAMP remoteproc/virtio
                  ↓
                 RPMsg
                  ↓
       standard RPMsg services
```

RPTUN lower-half 接口：

- `nuttx/include/nuttx/rptun/rptun.h:347-370`

主要 ops：

```c
get_cpuname
get_firmware
get_addrenv
get_resource
is_autostart
is_master
config
start
stop
notify
register_callback
reset
set_phase/get_phase
```

通用驱动负责：

- remoteproc lifecycle。
- resource table。
- virtio device。
- vring。
- RPMsg nameservice。
- `/dev/rptun/<cpu>`。

- `nuttx/drivers/rptun/rptun.c:805-858`
- `nuttx/drivers/rptun/rptun.c:1305-1366`
- `nuttx/drivers/rpmsg/rpmsg_virtio.c:756-857`

### 7.2 master 与 remote

推荐：

```text
CPU0 CP NuttX UP        = RPTUN boot/control master
CPU1+CPU2 AP NuttX SMP  = 一个 RPTUN remote peer
```

注意：RPTUN `master` 主要决定 boot/config/start/stop 权限，不天然等同 resource-table author 或固定 virtio role。实现时必须单独确定：

- resource table 的唯一初始化方。
- DRIVER/DEVICE role。
- shared table ready phase。

第一版建议由 CPU0 在启动 AP 前初始化固定地址的静态 resource table，AP 从同一共享地址读取；实际 role flag 在实现阶段对照 NuttX reference driver 验证。

当前 checkout 的补充结论：

- `rptun_do_start()` 在两端都会调用 lower-half `get_resource()`；
- `is_master()` 控制 lower-half `config/start/stop`；
- virtio role 由 `is_master()` 与 resource table `reserved[0]` 异或计算；
- 当前 RPTUN core 没有评审所称的 `rptun_init_mem()` 调用。

因此 table 唯一 author、ready/generation/CRC polling 和 warm-restart stale 防护必须由
BK7258 lower-half/control header 明确定义，不能假设 RPTUN core 自动处理。

### 7.3 可参考 lower-half

优先参考：

- NRF53 IPC：`nuttx/arch/arm/src/nrf53/nrf53_rptun.c`
- STM32H7 HSEM：`nuttx/arch/arm/src/stm32h7/stm32_rptun.c`
- MPFS IHC mailbox：`nuttx/arch/risc-v/src/mpfs/mpfs_ihc.c`
- K230 IPI：`nuttx/arch/risc-v/src/k230/k230_rptun.c`

BK7258 与 MPFS/NRF53 最相似的部分是：

- 固定共享内存。
- mailbox/IPI notify。
- master 负责启动 remote。
- 静态 resource table。

### 7.4 不需要自研的内容

采用 RPTUN/RPMsg 后不需要自行设计：

- endpoint 地址管理。
- nameservice。
- virtqueue/vring。
- 通用消息 buffer 生命周期。
- 通用 socket/port router。
- 通用 service discovery。

仍需 team-owned 的只有：

1. BK7258 RPTUN lower-half。
2. AP boot-ready/heartbeat/PM 等少量管理 service。
3. Wi-Fi controller/frontend 专用 service。
4. BK7258 特有异常与恢复 policy。

---

## 8. 可复用的 NuttX RPMsg services

| 需求 | NuttX service | 建议 |
|---|---|---|
| AP 日志送 CP UART | `syslog_rpmsg` | 首批启用 |
| bring-up ping/echo | `rpmsg_char` | 第一条数据链 |
| AP 访问 CP filesystem | `rpmsgfs` | CP 持有 filesystem 时首选 |
| AP 访问 CP raw MTD | `rpmsgmtd` | 仅在 AP 独占挂载 FS 时使用 |
| block device | `rpmsgblk` | 按需 |
| BLE Controller/Host split | `bt_rpmsghci` | 与官方 owner 高度匹配 |
| 虚拟 TTY | `uart_rpmsg` | 需要终端语义时使用 |
| reset | `reset_rpmsg` | 可复用 |
| clock | `clk_rpmsg` | 可复用后扩展 BK DVFS |
| regulator | `regulator_rpmsg` | 可复用 |
| 网络 socket proxy | `usrsock_rpmsg` | 阶段性方案 |
| L2 virtual network | `rpmsgdrv` | 可作为 Wi-Fi data-path 参考 |

主要源码：

- `nuttx/drivers/rpmsg/rpmsg_char.c`
- `nuttx/drivers/syslog/syslog_rpmsg.c`
- `nuttx/fs/rpmsgfs/rpmsgfs_client.c`
- `nuttx/drivers/mtd/rpmsgmtd.c`
- `nuttx/drivers/misc/rpmsgblk.c`
- `nuttx/drivers/wireless/bluetooth/bt_rpmsghci.c`
- `nuttx/drivers/serial/uart_rpmsg.c`
- `nuttx/drivers/reset/reset_rpmsg.c`
- `nuttx/drivers/clk/clk_rpmsg.c`
- `nuttx/drivers/power/supply/regulator_rpmsg.c`
- `nuttx/drivers/usrsock/usrsock_rpmsg.c`
- `nuttx/drivers/net/rpmsgdrv.c`

---

## 9. 最终推荐架构

```text
CPU0 / CP / NuttX UP
├── AP image boot/reset/power
├── RPTUN master
├── mailbox doorbell
├── Wi-Fi controller / PHY / RF / coexistence
├── BLE controller
├── Flash / PM / WDT
├── RPMsg BT-HCI server
├── RPMsg Wi-Fi backend
├── FS/reset/clock/regulator servers
└── AP heartbeat and recovery supervision

               shared SRAM + mailbox
                     RPMsg

CPU1 / AP local logical core0
CPU2 / AP local logical core1
└── one team-owned NuttX SMP image
    ├── CPU1 receives CP/AP mailbox IRQ
    ├── CPU1 starts CPU2 internally
    ├── RPTUN remote
    ├── Wi-Fi frontend/netdev
    ├── NuttX TCP/IP and POSIX sockets
    ├── NuttX Bluetooth host
    ├── multimedia/application
    └── syslog/FS/system-service clients
```

关键原则：

1. CP/AP 只建立一条 RPTUN link。
2. CPU2 只属于 AP SMP，不单独注册 RPMsg peer。
3. RPMsg 用于控制面和中小数据。
4. 大型 multimedia buffer 放 shared PSRAM，用 RPMsg 传 descriptor/ownership。
5. Wi-Fi packet path 初期可复制，后期根据吞吐升级 shared packet ring。

---

## 10. SRAM、PSRAM 与 cache

官方 v3.1.1.9 FreeRTOS 镜像生成布局：

```text
AP_SPINLOCK  0x28000000 size 0x10000
AP_RAM       0x28010000 size 0x54000
CP_RAM       0x28064000 size 0x3B700
PWR_MNG      0x2809F700 size 0x100
SWAP         0x2809F800 size 0x800

CP_PSRAM_HEAP     0x60700000 size 0x20000
AP_PSRAM_HEAP     0x60720000 size 0xA0000
AP_PSRAM_SECTION  0x607C0000 size 0x40000
```

- `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9/build/bk7258/app/partitions/ram_regions.h:6-29`

### 10.1 与团队双 NuttX linker 的关系

当前团队双 NuttX 镜像采用自己的明确重分区：

```text
CP RAM             0x28000000..0x2804ffff
AP RAM             0x28050000..0x2809efff
team telemetry     0x2809f000..0x2809f6ff
PWR_MNG reserve    0x2809f700..0x2809f7ff
SWAP/tail reserve  0x2809f800..0x2809ffff
```

官方和团队的 `AP_RAM/CP_RAM` 是不同固件体系的 linker ABI；因为最终不运行官方
FreeRTOS CP/AP 镜像，分区不同本身不是冲突。

必须保留/审计的是：

- `PWR_MNG`：SDK 固定地址 PM state，当前 `pwr_clk.h` 字段覆盖 PSRAM、wake/reset、
  exception 和 PM vote。未发现所谓 flash shared-lock 字段。
- `SWAP`：官方 `.swap_data`/mailbox exchange buffer 区；团队首版保留尾区，同时
  审计最终 ELF 中 `.swap_data` 的实际落点。
- `AP_SPINLOCK`：官方 linker 给 `.sram_spinlock_section` 的 section，不是硬件寄存器
  保留区。团队无需照搬 64 KiB 地址，但 SDK archive 若仍带该 section，team linker
  必须显式映射到 AP-owned region 或拒绝链接，不能由 orphan section 落入 CP RAM。

N9-A 必须产出 official/team compatibility matrix、SDK absolute-address allowlist 和
最终 ELF section/literal verifier。

### 10.2 RPMsg carveout

`SWAP` 只有 2 KiB，只适合官方小命令交换，不适合作为正式 RPMsg resource table + two vrings + buffer pool。

必须新增团队定义的专用区域，例如：

```text
CP_AP_RPMSG
├── resource table
├── vring0
├── vring1
├── RPMsg buffer pool
└── ready/generation state
```

具体地址和大小需要由使用当前 `sizeof(struct rptun_rsc_s)`、`vring_size()`、
descriptor/buffer constants 的 layout calculator 确定，不能靠估算拍定。

### 10.3 cache

官方当前 app 配置两侧均关闭 `CONFIG_CACHE_ENABLE`，但 mailbox 源码已包含条件 cache
操作。当前团队 AP 还进一步验证了 `CCR.DC=0`，并用 MPU region 15 将
`0x28000000..0x3fffffff` 映射为 Inner Shareable Normal Non-cacheable。N9 首版沿用
这个明确 contract，不再混写成“non-cacheable 或 cache-off”。

未来 NuttX 若启用 cache，必须实现：

```text
sender:
  write payload/ring
  clean cache
  memory barrier
  mailbox kick

receiver:
  receive IRQ
  memory barrier
  invalidate cache
  consume payload/ring
```

最安全的首版方案是将 resource table、vring 和 control buffer 在两侧 MPU 中映射为 non-cacheable。

---

## 11. Wi-Fi/BLE 的落地方案

### 11.1 BLE

推荐最终形态：

```text
CP:
  Beken BLE controller/RF
  bt_rpmsghci server

AP:
  NuttX Bluetooth host
  bt_rpmsghci client
  GAP/GATT/application
```

需要确认 Beken controller-only library 是否能暴露标准 HCI transport，以及相关许可是否允许链接进团队 CP NuttX image。

### 11.2 Wi-Fi

推荐最终形态：

```text
CP:
  Beken Wi-Fi controller/PHY/RF
  BK-specific Wi-Fi RPMsg backend

AP:
  Wi-Fi netdev frontend
  NuttX network stack
  POSIX sockets
```

需要自定义但限制在 RPMsg service 层的内容：

- init/deinit。
- scan result。
- connect/disconnect。
- security/credential。
- country/channel/power-save。
- event notification。
- TX/RX packet descriptors。

可参考 `vendor_beken/beken_wlan.c` 的 NuttX `wireless_ops_s` 和 `netdev_lowerhalf` 骨架，但其 direct `bk_wifi_*` 调用要替换为 AP→CP RPMsg request/data path。

阶段性简化方案是将完整网络栈放 CP 并通过 `usrsock_rpmsg` 暴露给 AP；该方案有助于先验证功能，但不作为最终 vendor-aligned 设计。

### 11.3 无线二进制边界

在完成以下清单前，不能声称“Wi-Fi/BLE 完全源码独立”：

1. BK7258 Wi-Fi MAC/controller 来源。
2. RF/PHY/coexistence 来源。
3. WPA supplicant 来源。
4. BLE controller 来源。
5. calibration data/firmware 来源。
6. 静态库许可和再发布边界。

---

## 12. 建议实施阶段

### Phase A：dual-image 与 AP primary

目标：CP NuttX 启动团队 AP NuttX UP，暂不启 CPU2/RPMsg。

1. 固定 CP/AP flash partitions 和 CRC pack 规则。
2. 创建 AP linker script，入口为 AP local core0。
3. CP 实现 CPU1 power/boot/reset sequence。
4. AP early trace 或共享 boot-state 可观测。
5. 验证 CPU1 physical ID、VTOR、stack、clock、timer。

验收：CPU0 NuttX 保持稳定，CPU1 独立执行团队 AP image 并报告 ready。

### Phase B：AP NuttX SMP

目标：CPU1/AP local core0 启动物理 CPU2/AP local core1。

1. AP local/physical CPU ID mapping。
2. secondary vector/stack/VTOR/NVIC/cache 初始化。
3. NuttX SMP scheduler hooks。
4. CPU1↔CPU2 IPI。
5. shared spinlock/critical section。
6. per-core timer与 interrupt affinity。

验收：两个 AP logical CPU 均进入 NuttX scheduler，能执行固定 affinity task 和 IPI test。

### Phase C：RPTUN transport

目标：CPU0 CP 与整个 AP SMP cluster 建立一条 RPMsg link。

1. 分配 `CP_AP_RPMSG` carveout。
2. 实现 CP/AP 两侧 `bk7258_rptun.c`。
3. static resource table 和 role/phase。
4. mailbox notify/register_callback。
5. CPU1-only RPMsg IRQ affinity。
6. `rpmsg_char` ping/echo。

验收：双向 endpoint 创建、nameservice、重复 reset/reconnect 均通过。

### Phase D：基础系统服务

按顺序启用：

1. `syslog_rpmsg`。
2. boot-ready/heartbeat/recovery management endpoint。
3. `rpmsgfs`。
4. reset/clock/regulator services。
5. 按需加入 UART/MTD/block。

### Phase E：BLE

1. CP controller initialization。
2. HCI transport over RPMsg。
3. AP NuttX Bluetooth host。
4. scan/advertising/connect/GATT regression。
5. AP restart、CP restart和 controller recovery。

### Phase F：Wi-Fi

1. CP controller/PHY/RF bring-up。
2. Wi-Fi control endpoint。
3. AP wireless netdev frontend。
4. packet data path。
5. scan/connect/DHCP/DNS/TCP/UDP。
6. reconnect、power-save、AP reset、CP reset。
7. throughput 和 zero-copy/shared-ring 优化。

### Phase G：鲁棒性和低功耗

1. heartbeat timeout policy。
2. AP局部 restart 与整机 WDT policy。
3. crash dump。
4. shared-memory generation counter。
5. cache-on regression。
6. suspend/resume 和低功耗网络保持。

---

## 13. 明确禁止的错误方向

1. 不把 CPU0+CPU1+CPU2 描述为一个三核 NuttX SMP。
2. 不由 CPU0 直接启动/管理 AP secondary CPU2。
3. 不把 CP tree 的 `startup_cpu1/2.c` 当作 AP SMP 主参考。
4. 不把 `mb_ipc` 作为 RPTUN lower-half。
5. 不把 CPU2 注册为第二个 RPMsg remote。
6. 不使用 2 KiB `SWAP` 充当生产 RPMsg buffer pool。
7. 不在 cache-on 模式遗漏 clean/invalidate/barrier。
8. 不让 `AP_SPINLOCK`、`PWR_MNG`、`SWAP` 进入普通 heap。
9. 不把 `vendor_beken` 描述为完整源码无线移植。
10. 不在未审计无线静态库/固件前宣称完全无厂商二进制依赖。
11. 不直接修改官方 NuttX tree；所有 contest 修改留在 team overlay。

---

## 14. N9 后续未决项

1. BK7258 Wi-Fi/BLE/PHY/controller 的最终 source/archive/firmware 清单和许可。
2. N9 以后新增服务、无线固件和文件系统需求对现有 CP/AP flash 分区的容量影响。
3. 当前 non-cacheable 首版之后，cache-on 正式版的 MPU/cache maintenance policy。
4. Wi-Fi 最终选择纯 RPMsg copy、`rpmsgdrv` 还是 shared packet ring。
5. CP/AP heartbeat timeout 后采用 AP restart 还是整机 WDT reset。
6. AP secure-only 初版之后是否需要 TrustZone/Non-Secure 分层。

---

## 15. 当前收敛结论

1. `vendor_beken` 证明了“官方 SDK/HAL/静态库 + NuttX OSAL/IRQ/lower-half”模式可行，但它不是 CP/AP IPC 方案，也不是完整源码无线实现。
2. BK7258 最终应保持 CPU0 NuttX UP + CPU1/CPU2 独立 NuttX SMP 两个系统、两个镜像。
3. AP primary 是物理 CPU1/AP logical core0；AP secondary 是物理 CPU2/AP logical core1。
4. CPU2 从同一个 AP image 的 `__vector_core1_table` 启动，没有独立 CPU2 image。
5. CP/AP 不需要自研通用 IPC，应采用 NuttX RPTUN/OpenAMP/RPMsg。
6. team-owned 部分应限制为 mailbox RPTUN lower-half、boot/heartbeat/PM 和 Wi-Fi 等 BK7258 专用 service。
7. 正式 RPTUN 只复用 mailbox HAL/IRQ、boot/reset、memory/cache primitives，不继承 `mb_ipc` 私有协议。
8. AP SMP cluster 对外只表现为一个 RPMsg remote；CPU2 不单独参与 CP/AP transport。
9. BLE 最适合使用 CP controller + AP NuttX host + `bt_rpmsghci`。
10. Wi-Fi 最终推荐 CP controller/backend + AP NuttX netdev/network stack，并在 RPMsg 上实现 BK7258 专用 control/data service。

Phase A、N7 AP primary、N8 AP SMP 和 N9 RPTUN/RPMsg wrapper 均已完成并取得板端证据。
N9 冻结结果为：32 KiB carveout、CP resource/master + AP remote、SDK mailbox channel
wrapper、AP CPU0-only OpenAMP gateway、shared pending level state、动态 Name Service、
generation reconnect 和 `syslog_rpmsg`。下一项工程工作应在这个 transport 基线上选择
独立服务，不再回到 N9-R/N9-A 规划状态。原 N9 计划与 17 项评审处置均为
已归档过程材料；[source verification](n9-rptun-source-verification.md) 保留作为
可复核技术证据，现役结论应以源码、配置和最新验证记录为准。
