# 2026-10-10 按需相机与电脑单帧取景

电脑工作台新增显式单帧采集、取消传输、查询及释放入口。原生 App 单独显示
并保存 camera 权限；旧 tasks/resources/diagnostics 授权不会自动获得相机。
设备复用现有 vision worker、V4L2 capture owner、PC TLS/control 和产品生命周期，
不新增采集线程，不常开相机，不调用云、不写 SD、不自动保存或上传原图。
完整输入、期限和使用步骤见[相机合同](../../platforms/bk7258/shaniu-pc-camera.md)。

## 软件验证

生产单请求状态机覆盖准入、重复身份、取消、采集错误、过期、时钟回退和下一轮；
生产 V4L2 等待/退出函数验证取消检查点、超时和 quiesce。真实 TLS、独立 PC grant
与设备控制服务贯通请求、取块、身份复核和释放，只有外部摄像头替换为明确的
自制 2×2 JPEG 夹具；该结果不表示实板画质。工作台 HTTP 测试验证取消和仅保留
最新预览，CLI 默认只返回统计。原生 Android 单测、标准 APK 48 构建通过。

首轮候选门禁发现旧非法权限向量仍为 16，新增 camera 权限已明确占用此位；
把未知输入改为 32，保留原拒绝断言。旧日志保留，不算生产权限绕过。采集器
分组自检还发现新增执行 ID 未列入 added_ids；只补分组，既有与新增执行集合、
断言和门槛不变。HTTP 取消测试的旧观察预期 200 修正为已有 API 的 202，保留
实际取消/释放断言，不计为业务 Red。

最终候选 `fc63e26e` 的完整门禁为 925 PASS，断言失败、环境错误、未运行及
收集错误均为 0，生产源码运行前后相同，两项既有变异均被检出。随后仅补报告
分组、固定测试依赖和本证据文档，执行 ID 与生产源码不变，原采集器 30 项自检通过。
冷 CI 显式安装 Pillow 10.4.0 解码自制 JPEG；全新隔离 Python 环境安装和解码
验证通过，未依赖开发机预装模块。

## HIL 身份与边界

- 运行组合树以开发提交 `1242847c` 构建；Agent 固定
  `20890a97b9515cce3de34006ad7a9b1746a109ed`，NuttX
  `76354c637858ecb0aa4601629327acb6f44a26bb`。独立相机候选基于 PR130
  `a21e2228f9d5d5e9bd80a1d66d4cf58cc35e22cd`，需要其本地内容准入接线，
  并经其依赖 PR129；不依赖 PR131 的主机任务发送器，也不默认串到 PR128。
  17 个本轮固件/Android 变更文件与 HIL 组合树逐文件相同，整树实板证据不互借。
- 当前端口确认 CH340 COM9 为控制/下载，原生 CDC COM16 为产品通道；不是将
  UART 当作 USB 产品能力。712 签名/计数/布局验证通过，BK Loader 只写 inactive A
  的 CP `0x11000+0x132000` 和 AP `0x143000+0x20f000`，写入标记通过。
  未执行 factory-init，未改 boot、校准、owner、资源、信任根或 OTP/eFuse。
- 实际运行 `0.7.66+712 counter=712 pair=confirmed active=A`；AP/CPU2 健康。
  经现有 `bkvision snapshot` 执行一次真实 V4L2 采集，返回 JPEG 640×480、
  13523 B、sequence=0、SOI/EOI 有效、v4l2_error=no，storage=disabled。
  返回路径已执行 stream-off/close，之后 supervisor faults/recoveries 为 0。
  未保存画面、未听看画面，也未把该 UART 后端采集当作 PC 授权纵向通过。
- 采后 motion 缓存 timestamp_us=149268375，来源 periodic-cache；电池 charging
  4145 mV，percent unavailable。原默认 shaniu-cyan-v3 revision=3 保留；显示
  仍 WAITING_ASSET，未宣称 LCD 提交、真人视觉/手感通过。
- 原生 USB 正向传图仍缺合法 camera profile；前一原生 CDC 写超时现场保留，
  没有循环重试、伪造 owner 或使用过期云凭据。授权、真实 USB 带宽/预览画质、
  光照/距离和长期稳定性为独立待验层。

## 资源与交付

单请求槽，采集预算 8 s，成功缓存保留至多 120 s；102400 B 按需缓存，另复用
驱动单帧 MMAP。控制复制 16 B 分块，长 I/O 和清除释放在锁外。无新增线程、
计时器或 SD 写入。编译器静态帧 bkcamera_work=104 B、bkcamera_control=136 B，
不是全调用链峰值；实际 AP heap 峰值、驱动最长阻塞和 USB 单帧耗时待验。
AP raw 1863216 B，比 N2 增加 3056 B；CP raw 1115880 B 不变。

- 标准 712 私有签名包 SHA-256：
  `15415ae1ce361167f27f47bae600633a99a278d89e6352ad8a18e1f93a0a6f00`。
- 标准 Android 48，`0.7.18-shaniu-desktop-camera`，APK SHA-256：
  `fd4c1c796f79d7f832f66991f895dbaed53effecefae5823311ed165db2cf1a7`。
- 本地证据目录 `out/shaniu-desktop-20261010/camera/`、`hil-712/`、`state-712/`
  及 `final-712/`；APK 在 `delivery/`。私钥、owner/profile、同板私密 BIN 和
  原始设备身份不随 PR 发布，合成测试夹具明确标识。

USB Audio、标准 UVC/HID、家庭事件和更多小游戏继续保留为后续独立专项；
本轮没有并行启动这些专项。
