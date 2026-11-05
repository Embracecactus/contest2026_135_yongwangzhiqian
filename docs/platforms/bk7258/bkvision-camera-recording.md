# BKVision 摄像头与连续录像

AIDK 的 AP 通过 NuttX `/dev/video0` 提供 GC2145 JPEG 采集，CP 的
`bkvision` 命令通过 RPMsg 请求 AP 工作。默认使用 640×480、30 fps；
传感器寄存器控制通过标准 V4L2 ioctl 提供。

```text
bkvision snapshot
bkvision record 10
bkvision check-record <session:8hex> <sequence:8hex>
```

`snapshot` 返回一帧的格式、长度和 JPEG 边界检查结果。`record` 接受
1–60 秒，生成 `/recordings/video-<session>-<sequence>.avi`，没有音轨。
`check-record` 只读打开指定录像，检查 AVI 头和文件长度，返回全文件
FNV-1a；它不替代主机上的逐帧 JPEG 解码。

## 分层与生命周期

- `app/bk7258/bk7258_vision_*`：V4L2 MMAP 缓冲、录像、文件命名及 RPC。
  写入先复制到应用缓冲，完成后才 QBUF；写满时同步刷出，结束时更新 AVI
  帧数和时间并执行 fsync。帧率来自成功保存帧数与采集时间。
- `nuttx/drivers/video/gc2145.c`：独立于 BK7258 的 imgsensor 控制实现，
  支持自动/手动曝光、手动曝光行数、增益和水平/垂直翻转。寄存器页切换、
  修改与恢复在同一传输锁内完成；自动曝光启用时拒绝手动曝光写入。
- `boards/bk7258/aidk_ai_toy`：电源、SCCB 路由和传感器绑定；为 VGA JPEG
  行缓存静态预留 20 KiB SRAM，压缩帧仍使用 PSRAM。
- `chips/bk7258`：I2C 全局驱动引用计数、DVP 回调和 PM 生命周期。
  MJPEG 从打开至 SDK 关闭完成持有独立 480 MHz 频率请求。
  停止交付后由 SDK 完成 JPEG 关闭，避免过早复位中断关闭握手。

AP 的本地文件系统访问与 MSC 导出共享块设备租约：录像期间切换 MSC
返回 `-EBUSY`；MSC 持有介质时开始录像也返回 `-EBUSY`。

## SD NAND 数据路径

AIDK 的 `ap-aidk` SDK profile 使用四线 SDIO、TX DMA 和硬件 I2C1。
TX 每次最多 16 个扇区，先复制到独立的 8 KiB SRAM，再启动 SDK 阻塞写入。
DMA 仍在运行或写入失败时停止通道并屏蔽完成中断，随后才释放缓冲所有权。
RX 使用 CPU FIFO 与原生单块 CMD17；NuttX 读限制为一个扇区。

SDK 的写数据路径使用多块模式，因此单块 CMD24 在该 DMA profile 下转换为
CMD25，并在数据完成后发送 CMD12。SDK 补丁修正 TX FIFO 中断掩码更新顺序，
并让完成判断同时检查 FIFO 状态和中断使能。

容量继续由 NuttX 解析 CSD。已测试的板卡报告 247808 个 512 字节扇区，
即 126877696 字节（121 MiB），没有覆盖容量的常量或启动写入探针。

## 历史缓存复现观察

以下是当时工作区使用既有缓存完成录像构建的观察，不是当前源码复现的前提。当前冷构建遵循
[构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md#one-host-entry)。该历史基线使用此前验证的 OpenAMP/libmetal 缓存，其中 OpenAMP 带有
`VIRTIO_RPMSG_F_CPUNAME` 与 `fw_rsc_config` CPU 名称扩展；当时的上游 BK7258
代码已依赖它，而当时官方 NuttX 固定源码和自带补丁尚未完整包含该扩展。
直接从官方源码重新展开 OpenAMP 会在既有 RPTUN 代码处编译失败；本次未
纳入 OpenAMP 源码或兼容补丁，源码编译结果以既有缓存为前提。当时的 NuttX 改动在隔离副本应用；
原 `nuttx/patches/README.md` 链接已删除，不能作为当前复现入口。
SDK rebuild 只修改临时克隆；固定 SDK checkout 保持不变。

```sh
python3 tools/bk7258/bk7258.py sdk rebuild --profile ap-aidk \
  --source ../vendor/beken/bk_avdk_smp --jobs 12
make -C tests/host/bk7258 check
python3 tools/bk7258/bk7258.py build --board aidk_ai_toy \
  --boot direct --jobs 12 --clean
```

已有该 profile 的本地 bundle 时，rebuild 使用 `--replace`。`direct` 用于
源码编译检查；产品下载继续遵循项目的 MCUboot 构建与签名流程。

## 已有板上证据与限制

2026-09-07，清理后的源码通过 `make -C tests/host/bk7258 check`（含摄像头、
录像、SDIO SRAM 和头文件回归），以及 AIDK AI Toy、T5-Board、T5AI-Core
三个板型的 CP/AP `--boot direct --clean` 构建，依赖前述既有 OpenAMP/libmetal
基线。AIDK 最终 ELF 的 JPEG 行缓存和 SDIO TX 暂存均位于内部 SRAM；
V4L2 控制、八个采集缓冲及单块读/多块写配置均在最终配置中启用。

2026-09-06 的 `18.6.321+381` 验证固件，在 10000 ms 内保存 301 帧
640×480 JPEG，AVI 大小 9207112 字节。复位前后及一次完全断电上电后的
重复读取，FNV-1a 均为 `f081c022`。主机导出的前后文件逐字节相同，SHA-256
为 `deeb46ba4b98f4081bdc7102896883d5fde1c41dfd791da0099b043a5b6d010c`；
301 帧均通过 FFmpeg 严格 JPEG 解码。

这是一个 10 秒录像与一次断电保留性验证。发布源码移除了当时使用的临时
诊断，重新构建 SDK 时保留上游语音依赖；该源码组合尚未重新刷板验证。
上述证据不代表长时间录像、全介质压力测试或其他板卡的硬件验收。
