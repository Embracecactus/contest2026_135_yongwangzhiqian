# BK7258 N14 PSRAM / timer wrapper 源码复核

> 日期：2026-08-03
> 状态：**source-verified + build-verified + board-verified**
> 对应 N14 PSRAM Stage 计划已归档；本文件仅记录源码核验。
> 自动门禁：`tools/bk7258/verify_bk7258_psram.py`

## 1. 结论先行

N14 采用的仍是 Beken 官方 SDK 推荐的 owner/wrapper 模式，不是重写 SDK driver：CP 链接 official
v3.1.1.9 archive并通过最小 ABI调用 PSRAM/CPU1 PM vote；AP只通过 board allocator wrapper使用
已由 CP 初始化的 PSRAM。official NuttX、apps、SDK source和 SDK static library均未改动。

源码复核给出以下确定结论：

1. official CP 是唯一 PSRAM hardware owner；official AP `bk_psram_init()`明确 fail-closed；
2. normal runtime ownership入口是 `bk_pm_module_vote_psram_ctrl()`，而不是裸调
   `bk_psram_init()`；
3. official `projects/app` 默认仍采用低 8 MiB布局，但 SDK内存在完整 16 MiB layout template；
4. official AP `psram_realloc()`是 allocate-copy-free；official SMP heap使用 internal spinlock；
5. CPU1 release前需要 official power module 17 vote；`pwr_dw=0`不是等价替代；
6. PSRAM MPU contract是 region 6、non-shareable normal non-cacheable；每个 AP core都必须安装；
7. official timer callback是 daemon/task语义，不能直接在 NuttX watchdog callback中执行 SDK
   callback；
8. N14 board实现和上述语义一致，且 ELF owner/config/layout门禁全部通过。

## 2. 核验输入

| 输入 | 路径 / 版本 | 用途 |
|---|---|---|
| Beken product page | [BK7258 product page](https://www.bekencorp.com/index/goods/detail/cid/60.html) | 芯片宣称最高 16 MB PSRAM；不替代板端容量识别 |
| latest SDK source | `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9` | PM、PSRAM、MPU、clock、AP SMP allocator语义 |
| immutable SDK bundle | `board/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9` | CP/AP最终链接输入 |
| CP manifest | `bk_idk/manifests/v3.1.1.9/cp.sha256` | SHA-256 `438c1bf16a37cbfe13adda7e7e99c5f757c82d7b6cc04d61521ca1836155c7be` |
| AP manifest | `bk_idk/manifests/v3.1.1.9/ap.sha256` | SHA-256 `5d4b7908fd21201a5f5ec3537915209aaed0273dc9779d8ba72a40ab82056edc` |
| official NuttX/apps checkout | workspace sibling repos | wrapper兼容性与最终 zero-diff边界 |

产品页只回答“芯片能支持多少”；`0x8d08/0x8d1a`、anti-alias和 full-capacity boot test才回答
“当前 T5-AI板上实际装了多少且是否可用”。

## 3. official SDK 逐项核验

### 3.1 PSRAM owner、ID 与 PM ABI

| official source | 已确认事实 | board映射 |
|---|---|---|
| `cp/middleware/driver/psram/psram_driver.c` | CP `bk_psram_init()`执行真实硬件初始化，支持 scheduler启动前跳过mutex进入初始化 | 只由 CP archive内部经 PM vote触发 |
| `ap/middleware/driver/psram/psram_driver.c` | AP init注释为“psram only init in cp”并返回 `BK_FAIL` | AP wrapper禁止 PM/init，只建本地 heap |
| `cp/middleware/soc/common/hal/psram_hal.c` | ID从地址0读取；APS6408L/APS128XXO post-config分别形成`0x8d13/0x8d1a`；Winbond ID地址为`0x01000000` | board按 post-config与原始ID两种值识别，16 MiB还做 anti-alias |
| `cp/include/driver/pwr_clk.h` | `PM_POWER_PSRAM_MODULE_NAME_AS_MEM`枚举值10，声明PSRAM vote ABI | board最小ABI固定`AS_MEM=10`并由 verifier监测 |
| `cp/middleware/driver/pwr_clk/pwr_clk.c` | PM vote负责consumer状态、电压和实际init路径 | CP启动调用 vote一次，不裸调 init |

### 3.2 CPU1 power/release

| official source | 已确认事实 |
|---|---|
| `cp/include/modules/pm.h` | `PM_POWER_MODULE_STATE_ON=0`，CPU1 module枚举值17 |
| `cp/middleware/driver/pwr_clk/pwr_clk.c` | normal start先 vote CPU1 ON，再 `start_cpu1_core()` |
| `cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c` | SMP release同样先 vote，再写vector/reset |

board `bk7258_ap_start_locked()`现在固定执行：CPU1 vote → boot address → reset release。verifier对顺序
作 source gate。该修复解释了“寄存器 `pwr_dw=0`已写但 AP仍不启动”的板端现象。

### 3.3 layout 与容量

official `projects/app/partitions/bk7258/ram_regions.csv`标记`PSRAM_CAPCAITY_SIZE=8M`，并冻结：

| 名称 | base | size |
|---|---:|---:|
| `CP_PSRAM_HEAP` | `0x60700000` | `0x20000` |
| `AP_PSRAM_HEAP` | `0x60720000` | `0xa0000` |
| `AP_PSRAM_SECTION` | `0x607c0000` | `0x40000` |

SDK还至少包含两个完整结束于`0x61000000`的16 MiB模板：

- `projects/lvgl/freetype_font/partitions/bk7258/ram_regions.csv`
- `projects/lvgl/img_decode/partitions/bk7258/ram_regions.csv`

N14没有直接切换到某个LVGL应用模板。为降低跨阶段回归，首版保持`projects/app`低8 MiB ABI，
只把实测新增的上8 MiB标为 full-capacity boot-tested/reserved。

### 3.4 AP realloc 与 SMP heap lock

`ap/components/bk_rtos/freertos/mem_arch.c`中的 official `psram_realloc()`执行：

```text
tmp = psram_malloc(size)
→ copy
→ free(old)
```

`ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/portable/MemMang/heap_4.c`
用 `s_spinlock_heap`和 `HeapEnterCritical()/HeapExitCritical()`保护 allocator metadata。

这两点直接决定 board实现：

- 不调用 `mm_realloc()`，使用 bounded allocate-copy-free；
- 在所有 N14 allocator wrapper外层使用 internal-SRAM spinlock；
- NuttX private heap仍是原始实现，outer lock保证其 sleeping recursive mutex不会发生双核竞争；
- `mm_heap_s`也留在 SRAM，避免在 PSRAM上执行 exclusive-store更新。

### 3.5 MPU 与时钟

official MPU参考：

- `cp/middleware/soc/bk7258/mpu_cfg.c`
- `ap/middleware/soc/bk7258_ap/mpu_cfg.c`

N14冻结 region 6 `RBAR=0x60000002`、`RLAR=0x63ffffe3`、MAIR attribute 1 non-cacheable。
AP primary在 early start安装，AP secondary在进入 scheduler前独立安装；AP READY检查再次读取验证。

official clock参考：

- `cp/middleware/soc/bk7258/hal/sys_hal.c`
- official PM frequency table及`SYS_SWITCH_VDDDIG_VOL_DELAY_TIME`

v3.1.1.9语义是 320 tier `VDDD=0x7`、`VDDIG=0xe`、CPU0 speed bit 0=`/2`、CPU1/CPU2
bit 1=`/1`，settle decrement count 2600。board DVFS只修正自己的 wrapper/table/order；SDK不变。

## 4. board wrapper contract

### 4.1 初始化与 fail-closed顺序

`board_app_initialize()`顺序由 verifier固定为：

```text
AP control init
< Bluetooth IPC / PHY/RF calibration leaf >
CP PSRAM PM/init/full-capacity gate/private heap
PSRAM failure blocks AP
supervisor init
AP start
```

AP本地顺序固定为：

```text
per-core MPU
→ AP private heap
→ CPU0+CPU1 concurrent PSRAM test
→ RPTUN
→ Bluetooth
→ READY
```

### 4.2 allocator API

| API | contract |
|---|---|
| `bk7258_psram_malloc` | ready + outer spinlock + `mm_malloc` |
| `bk7258_psram_zalloc` | wrapper malloc后清零，不绕过lock |
| `bk7258_psram_realloc` | pointer必须属于本role heap；old-size查询、new alloc、bounded copy、old free |
| `bk7258_psram_free` | foreign pointer fail-closed；本地heap under outer spinlock |
| size/mallinfo APIs | 同一outer spinlock下读取；total只报告配置PSRAM region，不把SRAM control block算入 |

SDK OS malloc wrapper先区分 board PSRAM地址/heap membership，再路由到上述API；foreign PSRAM free
明确拒绝，不能误交给 internal-SRAM heap。

### 4.3 boot test

破坏性测试只在 CP、PSRAM PM vote之后、role heap/AP release之前运行一次，包括：

- data-bus walking-one；
- power-of-two address alias；
- 全容量 address-derived pattern；
- 全容量 inverse pattern；
- 全容量清零与校验。

`bkpsramtest`只提供非破坏性的 info/heap命令；不存在 runtime raw-capacity入口。

### 4.4 timer lifecycle

NuttX watchdog callback只更新状态并入队；`bk-sdk-timer` task执行 SDK callback。deinit在 callback
运行中只 detach handle并标记 delete；如果对象已经 queued，service entry在 callback返回后完成
final free。`bktimertest`同时验证普通 self-delete和 long-callback queued self-delete。

## 5. 自动 verifier 门禁

`verify_bk7258_psram.py`检查：

- N14 profiles只在N13基础上增加批准的PSRAM/timer gate，且必须CP/AP成对构建；
- frozen 16 MiB physical window、低8 MiB official ABI和upper-8 policy；
- CP/AP source owner、init次序、MPU、boot test、allocator spinlock、realloc语义；
- official latest SDK source中的PM枚举、owner、ID、layout、realloc和SMP lock未漂移；
- CP ELF必须含official PM/PSRAM symbols、timer test和board wrapper；
- AP ELF必须含consumer allocator，不得含`bk_psram_init`/HAL/logger hardware-owner symbols；
- CP/AP map只引用同一个`v3.1.1.9` bundle；
- RPTUN、BLE GATT和dual-image原有verifier继续串行通过。

最终 JSON SHA-256：

- PSRAM：`c4e486b4b921e3a1a78b3362f639600cd879f09d887e43002aebf173e514a1bb`
- RPTUN：`ed4319db1c1093ee437f73f6c5292f35a31bd67bca3bc1caa5aa96d0be23dc54`
- BLE：`255962ea5f1107416c5c52ab8406b91f9d4933b4a54c3225eb5efa14ce088fa6`
- dual image：`e89610b569bacee13b2a3400c589f3c0c7b9e9d02b4b4830ee5367334916a78e`

## 6. 残余边界

- upper 8 MiB虽已全容量测试，但没有 allocator/cache/DMA owner，不可当作已开放内存；
- 当前 non-cacheable策略优先保证correctness，没有性能SLA；
- role-local heap不是共享内存IPC，跨CP/AP传pointer仍禁止；
- board outer spinlock依赖所有PSRAM allocation都从wrapper进入；未来新增直调`mm_*`必须被verifier拒绝；
- factory首次校准与RTS cold已覆盖，物理断电、温压极限和长期memory stress不在N14验收范围。
