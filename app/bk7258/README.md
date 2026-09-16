# BK7258 board diagnostic built-ins

映射到 openvela `apps/system/bk7258`，由官方 `apps/system/`
CMake、Kconfig 和 Make 递归机制自动发现。
本目录只承载由 App Kconfig 显式选择的 BK7258 NSH 维护与诊断命令；初始化模板
`app/hello_app` 保持独立且不承载产品功能。每个命令都有独立的
`CONFIG_BK7258_APP_*` 开关；底层 Driver/Test
符号只负责能力/端点，不再自动注册应用。

可用 App 开关：

```text
CONFIG_BK7258_APP_BKVALIDATE
CONFIG_BK7258_APP_APCTL
CONFIG_BK7258_APP_RPMSG_TEST
CONFIG_BK7258_APP_RPMSGFS_TEST
CONFIG_BK7258_APP_BT_IPC_TEST
CONFIG_BK7258_APP_WIFI
CONFIG_BK7258_APP_PSRAM_TEST
CONFIG_BK7258_APP_GPIO_TEST
CONFIG_BK7258_APP_GPIO_IRQ_TEST
CONFIG_BK7258_APP_IRQ_TIMER_TEST
CONFIG_BK7258_APP_TIMER_SELFTEST
CONFIG_BK7258_APP_AGENT
CONFIG_BK7258_PRODUCT_KEYS
CONFIG_BK7258_APP_DISPLAY
CONFIG_BK7258_APP_HEALTH
CONFIG_BK7258_APP_NFC
CONFIG_BK7258_APP_VISION
CONFIG_BK7258_DISPLAY_SERVICE
CONFIG_BK7258_HEALTH_SERVICE
CONFIG_BK7258_NFC_SERVICE
CONFIG_BK7258_VISION_SERVICE
```

每个开关还带有 `_PROGNAME`、`_PRIORITY`、`_STACKSIZE` 子配置，可在
`menuconfig` 中调整。App 的 `depends on` 保证底层能力不满足时命令不可选。

N14 `cp_nsh_psram + ap_smp_psram`新增：

```text
bkpsramtest info
bkpsramtest heap [iterations=16]
bkpsramtest all  [iterations=16]
bktimertest [iterations=64]
```

`bkpsramtest info`同时核对CP容量/heap/MPU、boot-only raw gate和AP双核启动门禁；`heap/all`
只测试当前CP private heap。全容量破坏性PSRAM测试只在启动时、建立heap和释放AP之前执行，
不存在运行时raw命令。`bktimertest`验证SDK software timer callback的task context、callback内
self-delete及queued final-free。

完整范围、源码约束和实板证据见：

- [N14 board verification](../../docs/verification/bk7258/2026-08-03-n14-psram-board-verification.md)
- [N14 source verification](../../docs/platforms/bk7258/nuttx-port/n14-psram-source-verification.md)
- [N14 evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md)

P5 validation skeleton (opt-in with `CONFIG_BK7258_APP_BKVALIDATE=y`) exposes:

```text
bkvalidate list
bkvalidate run <descriptor-id>
bkvalidate all-compatible
```

The target-side table in `bkvalidate_main.c` is the sole descriptor source.
`all-compatible` serializes global resource claims and emits `SKIP` for interactive, fixture,
destructive-fault, planned, or unavailable requirements.  The dispatcher does
not call vendor SDK functions directly.  Individual descriptors may invoke
explicitly selected BK7258 diagnostic endpoints; they are never started merely
because the dispatcher is enabled.

## 测试分层约定

- 纯逻辑（无硬件依赖）的 C host 用例放 `tests/host/bk7258/`，唯一完整入口是
  `make -C tests/host/bk7258 check`。旧的维护工具 Python 测试目录已经退役。
- 命令壳（NSH 内置命令）留在 `app/bk7258/`，由 `CONFIG_BK7258_APP_*`
  门控；每个命令只有 enable/PROGNAME/PRIORITY/STACKSIZE 和依赖声明。
- CP/AP 公共生命周期契约通过 `app/testing/bk7258/` 的官方格式 CMocka 板上应用执行；
  三块板的 UART0 自动化由 `tests/pytest/test_bk7258/` 链入官方 pytest。需要显式操作
  硬件的 rpmsg / gpio / psram / bt / irq / timer 命令仍保留
`CONFIG_BK7258_APP_*` 形态，再由 pytest 调用，不在测试中复制产品实现。

## AIDK 官方 Agent 产品适配

产品伴侣名为“傻妞”，稳定 machine ID 是 `shaniu`。`CONFIG_BK7258_APP_AGENT=y`
把产品启动、受保护配置、云端 ASR/TTS 协议、唤醒模型和设备交互接到官方
Agent、voice channel、message bus 与 Media 主链。产品代码只保留服务协议、板级设备、
模型资产、App 控制和产品事件适配；对话上下文、会话与整轮语音生命周期由官方框架负责。

`CONFIG_BK7258_PRODUCT_KEYS` 使用标准 `/dev/buttons` 事件提供 K1/K3 音量和 K2 电源；
`CONFIG_BK7258_VOICE_KWS` 注册 Trigger 使用的本地模型后端。`BK7258_VOICE_TLS` 只提供
云端后端共用的受信 TLS 传输，不拥有 Agent 或会话。历史 `bkvoice` 命令、Gateway、PTT、
turn/session 和整轮 cloud runtime 已退出正式构建。

AIDK 的 SD NAND 只注册为 `/dev/mmcsd0`，不把 `/data` 误认为 SD NAND。
BKDisplay 仅在持有 USBMODE 块设备 lease 时短暂挂载它，并在释放给 MSC
之前完成卸载。双眼资源包的生成、首次拷贝、目录和回退契约见
[Shaniu eye assets](assets/display/README.md)。构建成功只证明源码与配置可链接；真实唤醒、
云端交互、播放完成、再次监听和 App OTA 仍须在同一候选固件上分别验收。

`CONFIG_BK7258_APP_DISPLAY=y` 在 CP 注册最小板端控制入口。命令通过独立的
`bkdisplay-v1` RPMsg 协议请求 AP 显示服务，CP 不直接访问 SD NAND 或 LCD：

```text
bkdisplay status
bkdisplay mood neutral
bkdisplay mood happy
bkdisplay mood listening
bkdisplay calibrate
```

`mood` 使用资源包中的逻辑 expression 名称，状态是易失的，重启仍回到
`neutral`。只有返回 `BKDISPLAY MOOD PASS` 且两块实屏画面符合预期，才算板端
显示闭环；`mapping=unverified` 仍表示左右物理映射尚未验收。
`bkdisplay calibrate` 只在正常资源画面已经就绪后运行：它把 `/dev/fb0`
显示为纯青色、`/dev/fb1` 显示为纯品红色，并保持
`mapping=unverified`。记录实体左眼看到的颜色后执行
`bkdisplay mood neutral` 恢复画面；校准证据由板级映射配置消费，App 不猜测或持久化结果。

## AIDK 设备健康 App

`CONFIG_BK7258_APP_HEALTH=y` 在 CP 注册只读 `bkhealth` 命令，AP 的
`bkhealth-v1` 服务通过标准 `/dev/bat0` ABI 读取充放电状态和毫伏电压，并通过
CP-owned 温度端点读取片上温度原始码：

```text
bkhealth status
```

命令允许电池或温度单项失败，并为每项打印独立状态；它不把某个传感器故障扩散成语音、
显示或 OTA 故障。当前没有经过电芯放电曲线标定，因此明确输出
`percent=unavailable`，不会用电压线性换算伪造电量百分比。温度原始码始终优先；只有为该
芯片提供有效的 25 摄氏度参考原始码后，才输出 `temperature_mC` 和
`calibrated=yes`。host 与目标构建通过仍不等于实板读数验收。

## AIDK NFC 在场 App

`CONFIG_BK7258_APP_NFC=y` 在 CP 注册 `bknfc`，AP 的 `bknfc-v1`
服务按次独占打开、读取并关闭 `/dev/nfc0`：

```text
bknfc scan
```

命令只返回 `present=yes/no`，协议中没有 UID、卡号或卡片内容字段；“检测到卡片”也不等于
身份认证或授权。读取只使用一个随即清零的私有 scratch 字节，任何 UID 派生内容都不会跨
RPMsg 或由命令打印。host 与目标构建仅验证协议、资源释放和隐私边界；仍需用无卡/有卡
重复扫描完成实板验收，后续白名单场景必须使用独立、可撤销且不能以 NFC 为唯一凭据的策略。

## AIDK 视觉 App

`CONFIG_BK7258_APP_VISION=y` 在 CP 注册 `bkvision`，AP 的
`bkvision-v2` 服务作为当前配置中 `/dev/video0` 的唯一应用 owner，经标准 V4L2 MMAP 路径
拍照或连续录像；当前 NuttX capture buffer 是设备全局资源，不能同时启动其他 camera app。
CP/AP 必须使用同一协议版本，v1 与 v2 不会建立连接：

```text
bkvision snapshot
bkvision record 10
bkvision bench 10
```

`snapshot` 只返回实际宽高、JPEG fourcc、有效字节数、capture sequence 以及 SOI/EOI/V4L2
错误标志。JPEG 数据仅在 AP 的 driver-managed PSRAM buffer 中短暂存在，用于边界校验；协议
没有像素或指针，snapshot 命令不会写文件。捕获使用非阻塞 DQBUF 和有界超时，
且仅在 STREAMON 成功后执行 STREAMOFF，随后结束映射视图并关闭设备；BK7258 的 flat-address
MMAP 缓冲区由 sole-owner 的最后一个 close 释放。2026-09-05 的 `18.6.230+290` 已通过
同次启动三次及正常重启后一次有效 JPEG 拍照和关闭，见
[SDK 频率投票修复与实板证据](../../docs/platforms/bk7258/aidk-gc2145-jpeg-frequency-fix.md)。
当同一 AP 配置同时启用 `BK7258_DISPLAY_SERVICE` 时，显式 `snapshot` 会复用现有眼睛资源包：
捕获前显示 `thinking`，捕获成功或失败后分别显示 `happy` 或 `error`，短暂保持后恢复
`neutral`。显示失败不会改写拍照结果；恢复使用条件替换，若语音等并发路径已经切换到更新
表情，则不会被迟到的 `neutral` 覆盖。
仍需完成超时恢复、100 次循环、fd/heap/Camera owner 泄漏和真实 Camera 隐私指示验收。
本纵切也不等于 Gateway VLM 或 Android snapshot 已接通。

`record <1..60>` 是同步、有时长上限的无声录像命令。AP 挂载已有 FAT
`/dev/mmcsd0`，使用三个 MMAP 缓冲循环 DQBUF/写入/QBUF，保持一次 STREAMON，
结束后 STREAMOFF/close。结果保存为卷内 `/recordings/video-<session>-<sequence>.avi`，
命令输出精确文件名、帧数、采集毫秒数和 AVI 字节数。已有文件不覆盖，不格式化存储；
单文件上限 64 MiB，写满、取帧超时、坏 JPEG 或关闭错误均报失败并尝试删除本次残片。
异常断电不保证删除残片或修复 AVI 头。启用 USBMODE 的固件正常完成后可通过 USB MSC
取走文件；当前 AIDK 配置未启用 USBMODE，尚需实板文件导出验收。

录像与显示资产操作由 `bk7258_media_volume` 互斥，共用 `/mnt/sdnand` 挂载点；若启用 USBMODE，
录像持有其 blockdev lease，USB MSC 与本地写入互斥。卸载失败保留 lease，下一次录像
先重试清理。不要从其他命令同时挂载该块设备或操作该目录。

AVI 使用实际交付帧数与采集用时计算平均播放帧率，不宣称固定 30 fps，也不表示无丢帧。
没有音频、实时推流或提前停止命令；到时自动停止，单次 DQBUF 最多额外等待配置的取帧超时。
主机验证与板端连续录像验收分别记录在
[连续录像适配](../../docs/platforms/bk7258/aidk-continuous-recording.md)。

`bench <1..60>` 使用同一连续取帧链路，但不挂载卷、不写文件。
`BKVISION PERF` 分别统计等帧与写文件耗时，`BKCAM PERF` 统计 SDK 完成回调、
缓冲耗尽及交付/丢弃次数；芯片统计覆盖 SDK 打开到关闭，包含启动阶段。
有效采集帧率按 BENCH 的 frames / elapsed_ms 计算，不能用 JPEG 中断数代替。

`record-verify <1..60>` 在录像结束后回读完整 movi 数据并核对 FNV-1a 摘要，
输出 `BKVISION VERIFY`；普通 `record` 不执行这项耗时检查。
这是落盘一致性检查，不等同于独立 JPEG 解码；结果中的 `elapsed_ms` 仍只统计
采集区间，回读发生在停流后，命令返回时间会更长。
