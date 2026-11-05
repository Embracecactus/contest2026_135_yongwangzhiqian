# AIDK 连续录像适配

## v287–v296 限制：显示共存与重启持久化当时未通过

本节记录 v287–v296 迭代当时的状态，不是现行状态。之后 `18.6.321+381` 的 10 秒录像与
断电保留结果见 [BKVision 摄像头与连续录像](bkvision-camera-recording.md)；
现行支持范围以 `boards/bk7258/CONFIGS.md` 为准。

`18.6.287+347` 在显示服务启用时，纯取帧 10.04 秒有 300 次 VSYNC、
202 次 JPEG 中断及 202 个有效帧。另一次 CP 独立计时的 `time "bkvision bench 10"`
总耗时 11.1813 秒（包括开关设备），AP 捕获区间 10 秒，298 次 VSYNC、200 帧。
传感器输入已接近 30 fps；当前缺帧在 YUV/JPEG 编码或后续交付链路，不能归因于
SD NAND 吞吐或简单修改 sensor FPS 表。历史 v284 的 30 fps 是单次特定配置结果，
不代表当前显示共存配置通过。

v286 关闭 ACMD23 后仍有持久化失败：188 帧的 4012116 字节 AVI 在重启前通过
全部帧独立解码和 Windows FAT 检查；重启后文件为零字节，4014080 字节数据链成为
孤立簇链。v287 不经过 MSC，录像后直接重启也返回 AVI 头校验错误 `-EBADMSG`。
因此预擦除选项保留为实验 patch，未选入主产品配置。

新增 `bkvision check-record <session:8hex> <sequence:8hex>` 使用只读挂载、新文件句柄，
检查完整 RIFF 长度和头部一致性并计算全文件 FNV-1a；它不进行 JPEG 解码。
v289 的分阶段日志确认该命令在约 9 秒内已读取完 893112 字节，先前未返回是因为
成功分支漏设 `operation_status=0`，CP 校验后丢弃回复。v290 修复后板端命令正常返回；
这不是文件读取死锁。读取成功只说明头部和长度有效，不能代替录制时预期哈希的比较。

v288 的 40 帧旧录像在 v289/v290 重启后仍有正确长度和头部，但 payload FNV-1a
从录制时的 `1ba3a189` 变为 `9e31c7d9`。Windows 导出和 v290 板端的全文件 FNV
均为 `77240a6d`；逐帧严格解码定位到第 40 帧，文件末尾缺少 JPEG EOI。
因此新增 `mmcsd/0003-wait-final-write-on-close.patch` 尚未通过持久化验收。
证据在 `out/bkvision-perf-v289/retained-v288-integrity.json` 和
`retained-v288-frame-audit.json`。`record-verify` 增加关闭并重新打开句柄后的 payload
比较，避免依赖写入句柄保留的 FAT 扇区缓存。

v289 的硬件状态计数为 300 次 VSYNC、202 个有效 JPEG、99 次 FIFO 满状态及
54 次编码慢状态；减少 YUV 留存诊断刷新并未改善帧率，实验已撤回。
同样撤回无效的 JPEG 自动码率禁用实验。读回 SYS 模式 1 寄存器 `00100030`
表明 core 选择 480 MHz，而 JPEG 仍选择 320 MHz/1；二者不能混淆。
v290 隔离测试按 SDK JPEG 示例配置 120 MHz 后无有效帧，未合入产品时钟策略。

v295 在同一次启动、同一显示启用配置下完成编码时钟对照：240 MHz 为
200 帧 / 10050 ms，320 MHz 为 200 帧 / 10030 ms，二者 DMA 长度不匹配数均为零；
480 MHz 为 124 帧 / 10020 ms，213 次 EOF 中有 88 次硬件字节计数与 DMA 长度不符。
该轮异常样本的 DMA 长度比硬件计数少 12～52 字节，但实际 DMA 数据首部为 SOI、
末尾存在 EOI。例如硬件计数 54539、DMA 有效字节 54503，EOI 也位于 54503。
因此此前的“尾部未到齐”只是候选解释，不能据此断言 DMA 丢了结尾；需要独立
解码判断码流中间是否丢数。v293/v294 分别在 SDK 清中断后、清中断前等待最多
100 次 1 us 轮询，均未消除长度差，等待改动已撤回。实验仅在隔离构建树内，
480 MHz 和长度修正均未作为产品修复合入。证据：
`out/bkvision-perf-v295/clock-240-320-480/serial.txt`。

v296 仅在隔离固件中按实际 DMA 长度保留异常计数样本，并记录样本长度与 FNV。
纯取帧为 200 帧 / 10000 ms；3 秒录像收到 54 帧，其中 31 次使用实验长度修正。
同句柄及关闭重开后的 payload `f76c20f2` 比较均通过，重启前只读全文件 FNV 为
`4411d704`。重启后头部仍报告 54 帧、1705316 字节，但后续读取返回 `-EIO`，
未得到完整哈希，因此不能把这次失败描述成已确认的哈希变化。
该样本的独立 JPEG 解码仍待 USB 导出；尚未证明长度修正后的码流完整。
证据为 `out/bkvision-perf-v296/` 中的 `sample-bench-record-export/serial.txt`、
`pre-reset-check-controls/serial.txt` 和 `post-reset-check/serial.txt`。

注意 SYS 的 AUXS/JPEG 辅助时钟寄存器由当前 BK7258 `sys_ll.h` 定义在 word `0x0a`
（地址 `0x44010028`）；v295 读回 `002785cc`。早期诊断使用 word `0x0b` 的值不能
作为该寄存器证据。编码时钟选择与分频仍在 word `0x08`，上述对照同时读回确认。

## 2026-09-06：30 fps 单次实板结果

`18.6.284+344` 在 COM8 同一 AIDK 上录得 **1800 帧 / 60000 ms = 30.00 fps**。
640×480 无声 MJPEG AVI 为 56202172 字节，板端完整 payload 回读 FNV-1a
`73aaf3fc` 通过，1800 帧源缓冲前后校验均未变化。导出后的同一文件通过 ffmpeg
全部 1800 帧解码，ffprobe 报告 `30/1`；SHA-256 为
`c6adbd8e6ac5e6599e81b560c6d9fb2ca55b23ef57d1e1264e70e3011810cd0c`。
Windows 只读 FAT 扫描未发现错误。随后删除该录像成功，重新录像 10 秒得到
296 帧 / 10030 ms，完整回读 `312b0ca4` 通过；短段包含开流等待，不能称为精确 30 fps。

验证配置为 SDIO 20 MHz、TX DMA、原生 CMD17 单块 CPU RX、每次至多 16 块写、
FAT16 16 KiB 簇、CMD13 就绪轮询不按系统 tick 睡眠，以及 8 个 V4L2 MMAP 缓冲。
`VIDIOC_S_PARM/G_PARM` 明确请求并读回 `1/30`；AE、曝光、增益和双向翻转控制的
查询、设置、读回及恢复均通过。SDK 仍承担传感器完整初始化，尚未改为独立通用初始化驱动。

SDK V2 SDIO ISR 原来直接使用未过滤的 FIFO 空状态，可能提前通知 TX 完成，或将 RX
完成事件送入 TX 分支。`ap-sdio-tx-start.patch` 现同时按 FIFO 空中断掩码过滤这两处判断；
实际 SDK ISR 的主机回放能够复现原始提前完成，补丁通过 TX/RX/重复通知用例。
chip 层另拒绝 DMA 仍活动时的成功返回，并在失败清理前保存 DMA 状态。
SDK 原始仓库保持不变，补丁只应用于规范 SDK 重建入口创建的临时副本。
这解释并修正了当前 DMA 路径的一项错误，不把所有历史 CPU FIFO 失败归因于同一问题。

原始证据 `out/bkvision-perf-v284/`（原始产物不可用，仅保留本文记录的摘要），包括
`mask-record60/serial.txt`、`avi-decode.json`、`fat-audit-after-record.txt` 和
`mask-remove-repeat-record10/serial.txt`。该轮启用 MSC 维护模式、关闭 AP 显示服务；
傻妞显示/摄像头共存及正式 USB OTA 配置需要后续独立验证，不能从本轮外推。
使用已应用 `nuttx/patches/` 的隔离构建树；未修改官方 NuttX checkout。

后续 `18.6.285+345` 恢复显示服务并重启后出现新的持久化失败：显示创建目录与录像
挂载准备均返回 `-ENOSPC`。Windows 交叉检查也发现根目录丢失、空闲空间为零；
保存的卷前 64 KiB 中第 4–127 扇区均为 `0xFF`。因此上述 30 fps/解码是成立的
单次运行结果，**重启后的存储持久化及产品共存尚未通过**。异常现场位于
`out/bkvision-perf-v285/`，未用 CHKDSK 修复覆盖。下一轮隔离验证关闭可选 ACMD23
预擦除提示，尚不能把该命令认定为故障原因。

## 接口与所有权

`bkvision record 10` 连续采集约 10 秒，将无声 MJPEG AVI 写入板载 NAND FAT 卷。
范围 1–60 秒、单文件至多 64 MiB；不自动覆盖旧文件或格式化存储。
CP 只负责命令与结果；AP 的 bkvision worker 独占 V4L2、文件、挂载和存储 lease。
`bk7258_media_volume` 为显示与录像提供非阻塞互斥，先取得所有权再 mount，直到卸载成功
才释放；启用 USBMODE 时同时持有其本地文件系统 lease。
采用既有 chip 层 CAMERA 频率投票，SDK open 到 close 的整个录像期间保持 480 MHz。
SDK 原始 checkout 未修改，当前静态库包含上述保留的构建补丁。

V4L2 MMAP 缓冲轮转，AIDK 当前选择八个，每次文件写入结束后归还对应缓冲。不为每帧重新打开相机，
不缓存整段视频，不把 JPEG 通过 RPMsg 搬到 CP。存储慢时仍可能出现丢帧；目前只报告
成功写入的帧数和总采集时间，没有声称精确统计传感器/SDK 丢帧。

协议升级为 `bkvision-v2`，需配套 CP/AP。同 session/sequence 的重试由现有服务
active/replay 机制去重；新文件使用 O_EXCL。录像是有界同步请求，断开命令端不会立即
中止，但到时或遇到取帧错误会退出。服务返回成功前完成相机关闭、AVI 头回填、fsync、
文件关闭和卷卸载。卸载失败保留存储 lease，避免仍挂载的 FAT 被 USB 导出。

## 文件格式

使用 [Microsoft AVI RIFF 定义](https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference)
的单视频流 MJPG、`movi/00dc` 帧块与偶数字节填充；不写可选索引，也不设置 HASINDEX。
帧率按实际成功写入帧数/用时回填，表达平均播放时序，不保留逐帧可变时间戳。
没有音轨、OpenDML 分段、掉电恢复或自动分卷。

## 验证

- `make -C tests/host/bk7258 run-vision-core run-vision-record run-media-volume`：协议边界、短写、EINTR、
  ENOSPC、零写、奇数字节填充、文件上限与无帧拒绝。
- 存储所有权测试覆盖显示/录像互斥、USB 忙时回滚、错误 owner 拒绝释放，以及释放失败
  保留占用后重试。当前 AIDK 配置启用 FAT，未启用 USBMODE，USB 互斥分支以主机测试验证。
- ASan/UBSan 验证 writer；当前执行环境 ptrace 不支持 LeakSanitizer，因此关闭 leak
  检测运行，不能将它列为泄漏验收。
- 用合成 640×480 JPEG 生成 30 帧 AVI，ffprobe 识别 MJPEG、30 fps、1 秒、30 帧；
  ffmpeg 完整解码无错误。它验证封装兼容性，不替代设备拍摄内容验收。
- CP/AP 使用 `build --board aidk_ai_toy --boot direct --clean --jobs 12` 构建验证。
  direct 产物仅用于构建检查，不是可下载到当前 MCUboot 设备的签名发布包。

板端 1/10/60 秒连续录像、重复录像及随后 snapshot 已通过下述有界用例。
仍需文件导出解码、存储满/忙/取帧超时恢复和 Camera/PM/fd/heap/存储 lease
长期计数验收；重复打开成功不能替代泄漏检查。

## 实板迭代

`18.6.231+291` 已通过签名下载和启动检查，但首段 1 秒录像在 `REQBUFS(count=3)`
返回 `-ENOMEM`，随后 close=0、CP/AP/RPMsg 仍健康。V4L2 一次申请整个 MMAP 池；
当时 DVP alloc 使用 AP 私有 PSRAM（640 KiB，其中 512 KiB 已预留给系统堆），
剩余空间不足以容纳 3×102400 字节。

后续修复位于 chip DVP 的 alloc/free：启用媒体池时将 MMAP 池交给
`bk7258_psram_media_malloc(BK7258_PSRAM_MEDIA_ENCODE)` 并配对 media_free；
未启用媒体池的配置保留私有堆路径。继续通过 V4L2 分配接口，未修改 SDK 源码或
全局 PSRAM 内存布局。v231 失败证据位于 `out/bk7258/aidk_ai_toy/hil-v231-20260905-001/`。

`18.6.232+292` 使用新一代签名完整下载后启动通过。首段 `bkvision record 1` 已成功：
REQBUFS count=3、9 帧、1040 ms、274428 字节、640×480，STREAMOFF/close 均为 0，
没有意外重启。成功返回路径包含 AVI 头回填、fsync、文件关闭及卷卸载；交付平均帧率
约 8.65 fps，不能当作 30 fps 或无丢帧验收。

随后 10 秒测试在 Windows 打开 COM8 时报告“函数不正确”，命令未发送；原端口状态
检查也无法打开。PnP 显示 CH340 COM8 存在且 Status=OK，但 Win32_SerialPort 未列出它，
未据此归因于板上硬件或相机 SDK。该次中断的证据见
`out/bk7258/aidk_ai_toy/hil-v232-20260905-001/acceptance.json` 及相邻原始 UART 日志。

重新插接后错误仍在；只读检查确认 COM8 映射到 CH340、未被 USBIP 占用，直接 Win32
CreateFile 也返回错误码 1。普通权限设备重启被拒绝，随后经 Windows UAC 以管理员权限
仅重启该 CH340 PnP 实例，COM8 打开及 v232 状态读取恢复。本次恢复未重新刷写固件。

恢复后的实际录像结果如下，均为 640×480、无声 MJPEG AVI：

| 用例 | 交付帧数 | 实际采集时间 | AVI 字节数 | 平均交付帧率 |
| --- | ---: | ---: | ---: | ---: |
| record 10 | 68 | 10100 ms | 2639248 | 6.733 fps |
| record 60 | 402 | 60030 ms | 15871316 | 6.697 fps |

两段均 REQBUFS=3、STREAMOFF=0、close=0、无意外重启；成功返回表示写入、AVI
头回填、fsync、文件关闭和卷卸载完成。第二段后 snapshot 取得有效 14455 字节 JPEG，
SOI/EOI 正确、V4L2 无错误、close=0。最终 v232 已确认，AP、CPU2 和 RPMsg 健康，
supervisor faults=0/recoveries=0。实际文件尚未导出并独立解码，不将合成文件解码
结果当作实拍文件的播放验收。

最新证据：`out/bk7258/aidk_ai_toy/hil-v232-20260905-003/acceptance.json`。
卷内文件为 `/recordings/video-2803195a-00000001.avi` 和
`/recordings/video-2803195a-00000002.avi`；CP 的 NSH 文件系统不直接提供这些 AP 路径。

v232 镜像 SHA-256：`f08e15ae51a7d640446ed84a2fafbc3f8a1d30c68c6e28dcffde407efa01468a`。
两轮临时私钥已删除，签名包及公开验证证据保留。


## 2026-09-05 性能分段对照

v18.6.233+293 增加 `bkvision bench <1..60>`：沿用录像的三个 V4L2
缓冲和 JPEG 校验，不挂载卷、不写文件。PERF 只输出计数与耗时。

| v233 命令 | 有效帧数 | 采集时间 | 等帧累计 | 写文件累计 | 最慢单帧写入 |
|---|---:|---:|---:|---:|---:|
| bench 10（录像前） | 200 | 10000 ms | 10000 ms | 0 | 0 |
| record 10 | 75 | 10170 ms | 140 ms | 10000 ms | 340 ms |
| bench 10（录像后） | 200 | 10000 ms | 10000 ms | 0 | 0 |

两次纯采集都是 20 fps。录像约 7.375 fps，写文件占采集区间约 98.3%。
录像期间 SDK 完成回调 256 次、错误 1 次、缓冲耗尽 0 次、丢弃 179 次、
交付上层 77 次；应用成功写入 75 次。芯片统计覆盖 open 到 close，包含
启动/关闭回调和停止时尚未消费的帧，不能与 AVI 帧数直接等同。

v233/v234 的 AP SDK 为 v3.1.1.9 ap-aidk：SDIO CPU FIFO、20 MHz、4 线，GDMA
分支未编入；NuttX `MMCSD_MULTIBLOCK_LIMIT=1`。SDK 原生 SD 卡驱动使用
CMD25 连续写，而当前 NuttX 使用单块写，不能直接套用 SDK 的吞吐假设。
另外 NuttX `mmcsd_transferready()` 默认睡眠 1 ms，当前 tick 为 10 ms；
上游已有 `MMCSD_CHECK_READY_STATUS_WITHOUT_SLEEP` 专门避免这种等待放大。
v234 仅启用此配置，保留单块传输，按同板对照验证收益。

SDK 的 GC2145 640×480 30 fps 表存在，实板 SCCB 读取宽度寄存器
0x97/0x98 为 0x02/0x80，配置请求是 FPS30，但这些不能证明实际 30 fps。
有效帧率仍按完成 JPEG 校验的帧数除以采集时间计算。

v233 原始证据：仓库 `out/bkvision-perf-v233/` 下的 `bench10`、
`record10`、`bench10-after-record` 与 `boot`。此轮未导出实板 AVI 解码。


v18.6.234+294 已完成同设备新签名完整下载，package/trust 校验通过，
设备保留区 `[0x7fa000,0x800000)` 与 v233 相同。

| v234 命令 | 有效帧数 | 采集时间 | 写文件累计 | AVI 大小 | 有效帧率 |
|---|---:|---:|---:|---:|---:|
| record 10 | 81 | 10100 ms | 10010 ms | 2648304 B | 8.020 fps |
| record 60 | 486 | 60180 ms | 59960 ms | 15994736 B | 8.076 fps |

60 秒录像关闭成功；SDK 完成回调 1518 次，错误 1 次，缓冲耗尽 0 次，
丢弃 1030 次，交付 488 次。写入吞吐约 266 KB/s，与 v233 的约 261 KB/s
接近。JPEG 平均大小也不同，因此不能把帧数变化全部归因于免睡眠配置。
该配置是当前实验镜像的一项调整，不代表存储瓶颈已经修复。

当前结论：尚未达到 30 fps。以本轮约 33 KB/JPEG 估算，30 fps 需要约
1 MB/s 持续写入；单纯增加有限帧队列只能吸收抖动，不能消除持续吞吐差距。
后续应验证 SDIO 连续多块写/SDK GDMA，同时独立测量采集端实际帧率。
不能直接解除单块读限制：当前 CPU FIFO 多块读有已知预读溢出窗口；
GDMA 又需要重建匹配的 SDK 静态库，不能只改 NuttX 编译宏。

v234 原始证据位于仓库 `out/bkvision-perf-v234/`。本轮未导出实板 AVI
独立解码，亦未作 CPU 占用和长时间资源泄漏验收。

录像后 snapshot 再次通过 JPEG SOI/EOI 校验、close=0；最终 CP/AP/CPU2/RPMsg
状态正常，Supervisor faults=0、recoveries=0。实验签名私钥已销毁。


## 2026-09-05 SDK TX DMA 适配与下载中断

ap-aidk profile 新增 `CONFIG_SDIO_GDMA_EN=y`，通过规范入口从同一个
manifest 固定 SDK 提交干净重建，SDK 源码没有直接修改。bundle tree 为
`145e57bf500b54d12cb8791574bb17a87d27d11533fd615d72a7de6651bd58f8`。
chip SDIO lower-half 请求 TX DMA，RX 保留 CPU FIFO，仍保持单块传输。
SDK 会吞掉 DMA 分配失败，因此 lower-half 检查 DMA_DEV_SDIO 的通道归属；
没有通道即初始化失败。写入失败时停止 DMA 并屏蔽、清除完成中断，防止
迟到回调继续引用调用方缓冲区。下一次写入重新使能完成中断。

录像 writer 计算序列化 movi 数据的 FNV-1a 摘要，在停流、写完头部并
fsync 后完整回读核对，同时检查文件总长。读回失败会按不完整录像清理。
这是非密码学落盘一致性检查，不替代独立 AVI/JPEG 解码。RPC 为回读留出
额外等待时间，统计的 elapsed_ms 仍只覆盖采集。主机篡改与截断检测通过。

v235 已完成干净构建及 package/trust 校验，但实际下载在 62% 后连续
`LinkCheck Timeout`。2026-09-05 11:26:50 CST 停止失败下载器；工具记录
`status=failed`、`safe_prewrite_failure=false`，仅擦除成功，没有完整写入
成功证据。当前设备不能视为完整可启动固件，必须完整恢复。
失败属于 BK Loader 传输阶段，不能当作 TX DMA 固件运行失败；本轮没有
新的实板帧率或 DMA 文件回读通过数据。最后一次已确认运行版本仍是 v234。

失败证据：仓库 `out/bkvision-perf-v235/download/result.json` 与同目录
`bkloader.raw`。COM8 此轮打开前再次出现 Windows“函数不正确”，重启已确认
CH340 设备实例后预检读到 v234 正常；未将这两个现象推断为板上硬件损坏。


已准备 v18.6.236+296 MCUboot 完整恢复镜像，独立新签名、干净构建、
package/trust 和静态下载预检通过，保留区与最后已验收的 v234 基础相同。
镜像 8388608 字节，SHA256：
`171d60f426281abdb28b4f1dcb723fb23c828c09dde201881b0c79ad0505557c`。
产物位于工作区 `out/bk7258/aidk_ai_toy/aidk_ai_toy-v18.6.236-g296-video-tx-dma-recovery-full/`，
交付记录在本仓 `out/bkvision-perf-v236/handoff.json`。
准备完成时尚未执行 v236 下载，等待所有者明确授权失败后的重试；
当时板端仍是 v235 失败下载后的不完整状态（后续恢复结果见下节）。私钥均已销毁，签名好的完整镜像不依赖私钥即可下载。


## v236 恢复验收与 v237 回读命令修正

收到所有者重试授权后，v236 完整下载通过；实板版本为 18.6.236+296，
CP/AP/CPU2/RPMsg 正常，Supervisor faults=0、recoveries=0。软件重启采集到
两次 `BKSDIO TX DMA channel=0 RX=cpu-fifo`（初始化及切换总线宽度），
以及 `BSDIO BOOT PASS`。此前不完整 Flash 状态已经恢复。

- v236 `record 1`：9 帧 / 1020 ms，AVI 259004 B；写入耗时 960 ms，
  close=0，movi 全量回读 258780 B，FNV-1a `cc5f84c2`，校验 ret=0。
- v236 `bench 10`：197 帧 / 10030 ms，约 19.641 fps；SDK 缓冲耗尽 0。
- 回读约 259 KB 耗时近 5 秒，说明 v236 自动全量回读会令较长录像超过
  原 RPC 等待时间。因此 v237 增加显式 `record-verify <1..60>`，普通
  `record` 只做写完、fsync、关闭和卸载；全量回读仅由验证命令触发。
  验证命令沿用 RECORD 响应语义，读回失败仍删除不完整输出。新命令和
  篡改/截断检查的主机测试通过，v237 干净构建及 package/trust 校验通过。

v237 首次下载申请被自动审批拒绝，要求所有者单独授权；随后所有者
明确回复“允许”，下载与启动检查通过，板端已更新至 v237。
连续录像尚未通过，不能宣称 30 fps 已实现，实测失败详情见下节。
v237 镜像 SHA256：
`2c2c2b8429b218c3a3e4f2edffd12677c0b0722d4bc5d3ab9c84fefd706c9e44`，
大小 8388608 字节，同设备保留区与 v236 一致，临时私钥已销毁。
证据分别在本仓 `out/bkvision-perf-v236/` 与
`out/bkvision-perf-v237/handoff.json`。


## 2026-09-05 v237 下载通过，长录像失败

v18.6.237+297 完整下载通过全部 BK Loader 成功标记，启动后版本与
计数器一致。验收结果如下：

- `record-verify 1`：9 帧 / 1060 ms；movi 回读 261668 B，
  FNV-1a `2d00e58e`，ret=0，摄像头 close=0。
- `record 10`：102 帧 / 10040 ms，约 10.16 fps；JPEG 2488302 B，
  write_ms=9820、wait_ms=180、write_max_ms=310；普通录像未触发全量回读。
  单帧约 24.4 KB，比之前实验小，不能把帧率差异全部归因于 DMA。
- `record 60`：约 46 秒时 SDK 报 `sdio write data timeout`，
  `data_size=512, tx_transfered_len=512`，应用返回 operation=-5。
  失败前写入 447 帧、JPEG 11600445 B；write_ms=45990、wait_ms=270。
  SDK complete=1041、error=1、no_buffer=0、dropped=592、delivered=449；
  streamoff 和 close 均为 0。这不是完成 60 秒录像，也不是有效录像文件验收。
- 失败后重新 snapshot 通过 SOI/EOI、v4l2_error=no 与 close=0；
  CP/AP/CPU2/RPMsg 健康检查通过，Supervisor faults=0、recoveries=0。

SDK 源码核验范围为固定提交的
`ap/middleware/driver/sdio_host/sdio_host_driver.c`：
`sdio_dma_tx_finish()` 在 DMA 搬运结束后使能 FIFO 空中断；
`sdio_dma_write_fifo()` 随后等待 TX 信号量；DMA 配置下 ISR 仅 FIFO 空
分支发此信号量，普通 DATA_WR_END 分支的发信号代码被禁用。
BK7258 LL 的写中断清除函数只清 DATA_WR_END 与 TX_FIFO_NEED_WRITE，
因此不能直接断言它清掉了 FIFO_EMPTY。现有日志没有失败时的中断状态、
掩码和 WR_STATUS 快照，尚不能确认具体失效窗口。
DMA 字节计数完成不证明卡端写入成功，不应据此吞掉超时或伪造成功。

本轮确认的问题在 SDK SDIO TX 完成等待路径，未据此推断硬件损坏。
当前 DMA 方案没有通过长录像稳定性验收；后续需取得失败现场状态，
验证完成通知条件，再评估吞吐与 30 fps。
证据：本仓 `out/bkvision-perf-v237/` 下 `download-authorized-001`、`boot`、
`record-verify1`、`record10`、`record60`、`post-failure-snapshot`、
`post-failure-health`；汇总为 `handoff.json`。


## 2026-09-05 v238 SDK TX 启动顺序修正

SDK 的 `sdio_dma_write_fifo()` 原顺序是启动 DMA，然后设置 TX FIFO
时钟门控。BK7258 LL 通过位域读改写 `sd_cmd_rsp_int_mask` 的 bit13；
DMA 完成回调通过同一个寄存器的 bit9 使能 FIFO 空中断。若回调打断
任务态读改写，任务恢复后的旧值写回可能覆盖 bit9，导致完成通知丢失。
这是源码可确认的竞态窗口，但尚未证明它就是 v237 超时的实际原因。

团队补丁 `chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio-tx-start.patch`
将门控设置移到 DMA 启动前，超时时增加 status、mask、last_isr 快照。
补丁保留原完成等待与错误返回。规范 SDK rebuild 入口对临时 AP 构建
副本先检查再应用补丁；官方 SDK checkout 仍干净。
SDK bundle tree 为
`d4d70be162c32d611a152067b7b96bf4063c7e3c80f0e07251a42f68826a315b`。
静态库反汇编确认 bit13 的写回在 `sdio_dma_tx_start` 调用之前。

v18.6.238+298 干净构建、完整 package/trust 和同设备保留区校验通过，
独立临时签名私钥已销毁。8 MiB 镜像 SHA256：
`aa0e8a9500862a61cad1cac0934ffafe2d15a700f27e97b829065ca4290265dd`。
产物位于工作区
`out/bk7258/aidk_ai_toy/aidk_ai_toy-v18.6.238-g298-sdio-tx-order-full/flash/operator-aidk_ai_toy-v18.6.238+298.bin`。
首次执行被自动审批拒绝，要求所有者明确批准具体版本；随后所有者
对 v238 刷写问题回复“开始”，下载与启动已通过，当前板端为 v238。
本轮运行结果见下节。
构建、反汇编、目标预检与交付记录在本仓 `out/bkvision-perf-v238/`。


## 2026-09-05 v238 首轮实板复测

完整下载成功，启动确认 18.6.238+298。`record-verify 1` 完成 10 帧 /
1140 ms，AVI 308596 B；movi 全量回读 308372 B，FNV-1a `3769cbc9`，
ret=0，摄像头 close=0。

`record 60` 完成 530 帧 / 60150 ms，约 8.81 fps，AVI 15460144 B；
JPEG 15455150 B，wait_ms=130、write_ms=59940、write_max_ms=360。
SDK complete=1347、error=1、no_buffer=0、dropped=815、delivered=532；
close=0。本轮未复现 SDIO 写超时，但单轮通过不足以确认偶发竞态已完全
消除。普通长录像未做全量回读或独立 AVI/JPEG 解码。

录像后 snapshot 通过 SOI/EOI、v4l2_error=no、close=0。
存储耗时仍占录像时间的大部分，约 0.258 MB/s JPEG 写入吞吐；
帧尺寸与前轮不同，不能将 fps 差异当成同条件性能变化。尚未达到 30 fps。
新证据位于本仓 `out/bkvision-perf-v238/` 下 `download-authorized`、
`boot`、`record-verify1`、`record60`、`post-record-snapshot` 和
`post-record-health`。

本轮最终 CP/AP/CPU2/RPMsg 健康检查通过，Supervisor faults=0、recoveries=0。


## 2026-09-05 v239 SDIO 汇总计时诊断镜像

NuttX `mmcsd_sdio.c` 的 `CONFIG_MMCSD_MULTIBLOCK_LIMIT` 同时限制读和写，
当前不能仅解除写限制而保持单块读；本轮没有复制或修改官方 MMCSD 驱动。
先通过 chip 层 `CONFIG_BK7258_SDIO_PERF` 汇总 CMD13、CMD24/25、其他
命令的次数与等待时间，以及 SDK write_fifo 的次数、成功字节、总耗时、
最大耗时和失败数。每 2048 次写入或一次写失败输出并清零一组统计；
命令字段为 次数/毫秒，时间分辨率由日志 tick_us 给出。首组包含初始化，
其他命令可能包含读操作；SDK 计时不覆盖 FAT 工作、read_fifo、数据配置和
命令前的时钟设置，不能将其直接等同于完整录像写入耗时。
失败时先停止 DMA 并复位数据状态，再输出统计。

v18.6.239+299 仅增加诊断，不宣称提高帧率。保留 v238 SDK 修正与单块
读写限制，AP resolved config 已确认诊断开关开启。干净构建、完整签名链
和保留区校验通过；独立临时签名私钥已销毁。所有者随后对本任务迭代授予持续授权，
v239 已刷入并通过启动检查。
8 MiB 镜像 SHA256：
`763c0cb2f5870ba0f3bb3f40de99db50855449918aa310e8a980a348c10bedc3`。
产物在工作区
`out/bk7258/aidk_ai_toy/aidk_ai_toy-v18.6.239-g299-sdio-perf-full/flash/operator-aidk_ai_toy-v18.6.239+299.bin`。
构建、package/trust 与交付记录在本仓 `out/bkvision-perf-v239/`。
本轮短录像回读和 60 秒汇总计时结果如下。


## 2026-09-05 v239 实板吞吐分解

短录像 9 帧 / 1030 ms，AVI 249544 B；movi 回读 249320 B，
FNV-1a `f92f5ad5`，ret=0。60 秒录像完成 634 帧 / 60150 ms，
约 10.54 fps，AVI 15018136 B，JPEG 15012206 B；write_ms=59990、
wait_ms=70、write_max_ms=350。无 SDIO 超时，close=0；录像后重新拍照通过。
SDK complete=1327、error=1、no_buffer=0、dropped=691、delivered=636。
帧尺寸与前轮不同，本轮只增加计时，不能将帧率差异称为优化收益。

共记录 14 个写入统计窗口。去掉包含启动及短录像的首个窗口后，
13 个窗口覆盖 13631488 B、26624 次写入：

- CMD13：52565 次，41930 ms。
- CMD24/25：26624 次，2080 ms。
- SDK write_fifo：5350 ms；其他命令：20 ms。
- 命令和写入错误均为 0。CMD13 占上述已计时操作约 84.9%。

NuttX `mmcsd_transferready()` 在每次前序写完成后的传输前通过 CMD13
检查 PRG/RCV 是否回到 TRAN；当前每块约 1.97 次查询。证据将主要开销
收窄到逐块写后的就绪查询路径，而非 DMA 数据搬运。计时包含命令响应与
任务等待，不能仅凭此区分卡端编程时间和调度开销，也不能跳过就绪检查。
后续优化应评估减少逐块事务次数及其通用读写限制接口；单块读限制仍需保留。
未达到 30 fps，未对长录像做全量回读或独立解码。

原始日志与解析结果分别在本仓 `out/bkvision-perf-v239/record60/`、
`out/bkvision-perf-v239/perf-analysis.json`。

本轮最终 CP/AP/CPU2/RPMsg 健康检查通过，Supervisor faults=0、recoveries=0。


## 摄像头标准控制的收尾边界

> 2026-11-04 现状：本节至 v249 记录 2026-09 的控制迁移。`nuttx/drivers/video/gc2145.c`、
> `nuttx/patches/`、`sdk-profiles/*.patch` 与 `bkvision control-test` 已在
> `129d4f9a`/`7079493e`（2026-09-16）移除；当前 AIDK 与 T5-Board imgsensor 不提供
> `get_supported_value/get_value/set_value`，`bkvision` 只有 `snapshot`、`record`、
> `check-record`。SDIO 完成判定修正以 SDK 提交 `eb0d425` 的修订形式保留。
> 2026-11-05 起固定 SDK 为其子提交 `b22c0e0`，DVP/GC2145 寄存器读写错误传到
> 调用者、读失败不发布值，SDK bundle 已重建；当前产品没有寄存器控制入口，
> 未做实板验收。见
> [DVP 寄存器错误传播](../../verification/bk7258/2026-11-04-dvp-register-error-propagation.md)。

应用继续使用 `/dev/video0` 的 V4L2 接口。曝光模式、曝光值、增益与帧间隔
应由标准控制/参数 ioctl 进入 imgsensor；GC2145 寄存器与单位换算归通用
NuttX 传感器驱动，DVP/DMA/I2C 及其互斥归 chip，供电和引脚绑定归 board。
board imgsensor facade 的 get_supported_value/get_value/set_value 当时转发到
通用 GC2145 控制模块；固定 1/30 仍是请求配置，不是有效帧率证明。

迁移时必须解决 SDK open 重置传感器与控制应用时机的关系，避免双重状态所有者；
不能把传感器曝光行数直接作为 V4L2 绝对曝光时间返回。控制错误应传播给 ioctl，
无效值不得修改硬件。验收包括控制查询、设置/读回、自动/手动切换、非法值、
STREAMON/OFF、重复打开、录像回读及关闭后的系统健康；物理画面另行观察。

用户允许将 NuttX 缺失能力及 SDK 修正保存为 patch。补丁应固定基线提交、
使用规范构建入口，并将构建检查与实板验收分别记录。摄像头验收完成后再逐项
推进 AIDK 已装配的其他外设；已有标准接口优先验证和补缺，不重复实现。


### 基础控制的实现与板测入口

GC2145 控制逻辑当时位于 `nuttx/drivers/video/gc2145.c`，由 board 的 imgsensor
回调转发；chip 的传感器访问锁覆盖整个选页/读写/恢复页面过程，并排斥
open/close。支持自动/手动曝光、曝光行数、全局增益（unsigned 4.4，16 为 1 倍）、
水平/垂直翻转。自动曝光开启时拒绝手动写曝光。绝对曝光时间和可变帧间隔
尚未接入；流初始化仍由 SDK 后端执行，因此不是完整传感器驱动迁移验收。

`bkvision control-test` 在 AP 通过 VIDIOC_QUERYCTRL/G_CTRL/S_CTRL 查询五项
控制，检查非法翻转值，切换手动曝光后逐项设置/读回，再恢复保存值并关闭。
这是会暂时改变传感器设置的诊断命令；成功必须包含 CONTROL TEST PASS，
并再运行 snapshot/record-verify 和系统健康检查。新命令为 v2 RPC 的加法扩展，
旧服务端应拒绝它；没有图片数据通过 RPC。

增益编号去歧义依赖 `nuttx/patches/video/0001-disambiguate-gain-controls.patch`，
SDK 错误传播依赖 `ap-dvp-register-errors.patch`。当时完整构建使用隔离验证树；
官方 NuttX checkout 未修改。主机测试覆盖传输错误、页面恢复、互斥和控制编号；
不能替代板端读回及物理画面观察。

### v245 标量控制接口回归

COM8 确认启动 18.6.245+305。控制测试首次 G_CTRL 返回 -EINVAL，尚未
修改控制：NuttX 的标量转换将 size 置零，GC2145 错误地只接受四字节。
驱动修正为接受零或显式四字节；NuttX S_CTRL 临时结构未初始化的独立
问题保留为 `0002-initialize-scalar-set-control.patch`。回归测试编译实际
G_CTRL/S_CTRL 转换函数并连接真实 GC2145 控制代码，覆盖读写与非法值。

失败后重新打开摄像头，短录像完成 19 帧 / 1060 ms，AVI 444960 B；
movi 回读 444736 B，FNV-1a `7e467c31`，ret=0，close=0。控制板测仍未
通过；v246 修正需要新固件板测，不能由主机通过推定。

### v246 设置路径复测与补丁位置修正

COM8 确认启动 18.6.246+306。五项 QUERYCTRL/G_CTRL 均成功，首次读回
AE=0、曝光=720 行、gain=85、HFLIP=0、VFLIP=0；设置及恢复返回 -EINVAL。
复查发现上一版补丁匹配了另一处同名注释，没有初始化 capture_s_ctrl。
旧主机测试未预填非零临时状态，不能稳定检出未初始化数据，曾错误通过。

修正后的补丁明确位于 capture_s_ctrl。主机回归在实际转换函数的临时结构
中预填非零值：保留初始化时通过，移除初始化的负对照必须失败。该修正
构建为 v247，后续板测结果见下节。

### v247 基础控制板测

COM8 确认启动 18.6.247+307。五项 QUERYCTRL/G_CTRL 成功，AE 自动转
手动、曝光 1250→1251 行、gain 85→86、HFLIP/VFLIP 0→1 均设置读回
成功；随后逐项恢复原值，AE 恢复自动。共 11 次设置/读回匹配，
非法翻转值被拒绝，CONTROL TEST result=0、restore=0。这里只证明
接口与寄存器读回，未验证物理画面方向和曝光时间单位。

控制测试后重新打开完成短录像 17 帧 / 1080 ms，AVI 449168 B；movi
全量回读 448944 B，FNV-1a `67d0e52e`，ret=0，close=0。无写盘取帧
203 帧 / 10040 ms，约 20.22 fps；SDK complete=204、error=1、
no_buffer=0、dropped=1、delivered=203，仍未达到 30 fps。

绝对曝光时间、可变帧间隔、完整传感器初始化归属和物理画面验收仍未
完成。基础控制通过不代表摄像头整体收尾，暂不推进其他外设代码修改。

本轮 60 秒录像未通过：记录 480 帧、JPEG 11202624 B 后提前退出，
operation=-5（EIO）；wait_ms=3620、write_ms=23000、write_max_ms=170。
STREAMOFF 和 close 均为 0，随后再次 control-test result=0、restore=0。
仅凭现有日志无法区分取帧、写入或存储容量问题；CP 控制台挂载 AP 存储
失败（errno=15），不能据此判断介质损坏或空间已满。未格式化或清理
历史录像。原始证据在 `out/bkvision-perf-v247/record60/` 与
`out/bkvision-perf-v247/post-record-control/`。

最后 COM8 健康检查通过，Supervisor faults=0、recoveries=0，RPMsg error=0。

### v248 满盘定位与 v249 清理后复测

v248 在 record-write 阶段返回 -EIO：录像前可用 5459 个 2048 字节块，
失败清理前 free=0、available=0，488 帧已写入，elapsed=27020 ms。
确认是存储写满；NuttX FAT 将无空闲簇转换成 EIO。修正保留为
`nuttx/patches/fs/0001-preserve-fat-allocation-errors.patch`，主机测试
区分 ENOSPC、无效簇链 EIO 和底层负错误，满盘新错误码尚未再次实板触发。

v249（18.6.249+309）按用户明确授权，逐个删除以下历史测试文件：

- `video-2801334b-00000002.avi`：15460144 B。
- `video-28013a5a-00000002.avi`：15018136 B。
- `video-28012486-00000002.avi`：24320608 B。

三个 REMOVE 均返回成功；删除后可用 32218 个 2048 字节块。
随后 60 秒连续录像通过：900 帧 / 60090 ms（约 14.98 fps），
AVI 23889228 B，JPEG 23880904 B；wait_ms=11950、write_ms=48020、
write_max_ms=260，close=0。SDK complete=1068、error=1、no_buffer=0、
dropped=166、delivered=902。此长文件未全量回读或独立解码。

长录像后 control-test result=0、restore=0；再次短录像完成 14 帧 /
1150 ms，AVI 459012 B，movi 全量回读 458788 B，FNV-1a `fdfc32e4`，
ret=0。该复测证明腾出空间后连续录像恢复，不代表达到 30 fps。
证据在 `out/bkvision-perf-v249/`。

容量核验：用户提供的 U7 照片确认型号为 `CSNP1GCR01-BOW`；其
[厂商数据手册 V1.1，第 1.1 节](https://atta.szlcsc.com/upload/public/pdf/source/20210222/C2691593_195F15B6690BC3EBF1684FE727FBFF20.pdf) 明确为 1Gb，
即标称 128 MiB，并非 1 GB。实板 CSD `007f0032 535a803c 6ebbff9f
00168000` 独立解析为 247808 个 512 字节扇区（121 MiB）。
原理图的“1GB”标注与这块板实际器件规格不符，容量继续依据 CSD，
不将软件报告值直接扩为 1 GB。
