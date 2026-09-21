# 2026-09-21 傻妞设备自主首启 + 屏幕扫码认领 + 离线蓝牙配置（实现与证据）

范围：把“PC 生成证书 → 串口供给身份 → 导入授权文件”从新设备主路径上移除，
改为设备自主首启、本机生成身份、屏幕显示二维码、手机扫码认领，随后在已认证
BLE 通道内配置 Wi-Fi 与语音服务。旧授权文件/`bkprov supply` 作为兼容入口保留。

生命周期与信任边界见
[shaniu-firstuse-lifecycle.md](../../platforms/bk7258/shaniu-firstuse-lifecycle.md)。

## 1. 实际修改

固件（CP）：

- `chips/bk7258/include/bk7258_factory_record.h`、`chips/bk7258/cp/bk7258_factory_record.c`：
  工厂事务记录（两个擦除扇区 A/B 槽、单调 generation、CRC32 仅检测损坏）。
- `chips/bk7258/include/bk7258_storage_guard.h`、
  `chips/bk7258/include/bk7258_storage_config.h`、
  `chips/bk7258/cp/bk7258_storage.c`、`boards/bk7258/common/src/bk7258_storage_config.c`：
  新增 `factory_record` 区域与 `BK7258_STORAGE_GUARD_FACTORY_RECORD` 写权限
  （存储配置版本 2 → 3）。
- `app/bk7258/bk7258_factory_main.c`：CP 命令 `bkfactory begin|mount|status`；
  `bkfactory mount` 取代 `rc.sysinit` 里的无条件挂载：只有 `PENDING` 才用冻结的
  LittleFS `autoformat` 初始化 `/data`，并把状态镜像写给 AP。

固件（AP）：

- `bk7258_provision_keygen.c/.h`：mbedtls 现场生成 EC P-256 密钥与自签叶证书
  （EKU serverAuth+clientAuth、KU digitalSignature、非 CA），组装既有 BPI1 记录；
  有效期来自发布模板常量（`BKPROV_IDENTITY_NOT_BEFORE/AFTER`），不读未校时 RTC。
- `bk7258_provision_firstboot.c/.h`：只读 CP 镜像、只对 `STORAGE_READY` 且身份
  缺失的设备生成一次身份；身份缺失且状态更高=故障，永不重生成。
- `bk7258_provision_qr.c/.h`：`SN1:` 版本化紧凑认领码 + 字节模式 QR 编码器
  （ECC L，版本 1..6）+ RGB565 整数缩放渲染。
- `bk7258_display_service.c/.h`：首用认领页（fb0 = 二维码，fb1 = 固件内置提示图形），
  不依赖 SD NAND 眼睛包；隐藏时清零含窗口秘密的帧缓冲。
- `bk7258_provision_owner.c/.h`、`bk7258_provision_pair.c/.h`、
  `bk7258_provision_claim.c/.h`：每次开窗新生成 32 字节窗口秘密；窗口同时接受
  旧授权秘密（兼容）；新增 `bkprov_owner_claim_code()`。
- `bk7258_provision_settings.c/.h`、`bk7258_provision_network.c`、
  `bk7258_control_session.h`、`bk7258_agent_product.c`：新增 `SCB4` 仅 owner
  认领记录（不需要 Wi-Fi/云），以及已认证控制通道的 kind 7（`SCB2` 网络+云记录，
  拒绝轮换控制密钥；提交后由产品重新应用网络）。

App（`android/shaniu-companion`）：

- 新增 `ClaimCode.kt`（严格解析，字段长度/重编码校验）、`QrScanActivity.kt`
  （Camera1 + ZXing core 3.5.3，前台扫码，不落盘）、
  `ProvisionBootstrap.fromClaimCode()`、`ProvisionSettings.encodeOwner()`。
- `ProvisionActivity` 新增“扫码添加傻妞”主入口：扫码 → 按 locator 找 BLE →
  固定指纹 TLS → 窗口秘密认领 `SCB4` → 直接进入 Wi-Fi/语音页并用控制通道
  kind 7 提交 `SCB2`，回读 `NW1` 确认。
- 版本 `0.5.24-shaniu-firstuse`（versionCode 29）。

## 2. 已取得的证据

主机（QR 编码正确性）：

- 用独立参考实现（Python `qrcode`）逐位比对：版本 5、全部 8 个掩码
  `diff=0`；版本 6 双分块（掩码 0/3/7）`diff=0`；版本 1..6 版本选择随负载正确。
- 认领码负载回环（含 103 字节真实负载）与 160×160 RGB565 渲染几何验证：
  `version=5 dimension=37`，模块 3 px，静默区 4 模块（bbox 24..134）。

构建：

- `tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct`：PASS，
  `sources=515 kconfig=252`，layout `bk7258-559f52beaec8a54e`；新符号
  `bkprov_identity_generate`/`bkprov_firstboot_identity`/`bkprov_qr_encode`/
  `bk7258_display_show_claim`/`bkprov_owner_claim_code` 在 AP 镜像中，
  `bkfactory_main`/`bk7258_factory_record_{read,write,advance}` 在 CP 镜像中。
- App：`./gradlew :app:assembleDebug` BUILD SUCCESSFUL；
  `app-debug.apk` 8,997,256 B，SHA256
  `b0be0721d50176390b7d0e6453f7348647bf0d9fe7a793aa6193f2e46e6e7130`，
  `versionCode=29 versionName=0.5.24-shaniu-firstuse`。

工厂镜像组包（未烧录）：

- 基底=本板 accepted base（`device_id C8:47:8C:CB:7F:80`，8 MiB）；
  覆盖 `boot/cp/ap/pair` 四段，擦除 `usr_config` 前 8 KiB 与 `persistent_data`。
  结果：8,388,608 B，SHA256
  `9b246bf0f2b1720bea02ec78d93cce77b910685457cfd3a0f912e7bc1b7ba44a`。

## 3. 未完成 / 阻塞

- **实机首启闭环未验证**：BK Loader 连接 COM13 失败
  （`Current port : COM13 + BaudRate : 115200 connect failed` →
  `Writing Flash Failed`），原因是该串口被本机 MobaXterm 占用：同时刻从
  PowerShell 打开同一端口返回“对端口 COM13 的访问被拒绝”。因此本次没有任何
  烧录、启动或功能通过结论。
- 由此未完成：`bkfactory begin` 写入、自主初始化、身份生成、二维码实拍扫码、
  手机离线认领、Wi-Fi/云配置、首次真实语音、掉电恢复与 3 轮重复性。
- 双屏二维码的实拍距离/角度与亮度结论未采集；用户侧“恢复出厂”设备入口未实现。

## 4. 状态表

| 项目 | 状态 |
| --- | --- |
| QR 编码器（主机逐位比对、渲染几何） | 主机通过 |
| CP/AP 固件构建与符号 | 仅构建通过 |
| App 构建（扫码/认领/控制通道配置代码） | 仅构建通过 |
| 工厂整片镜像组包 | 已生成（未烧录） |
| 工厂事务写入、自主首启、屏幕二维码 | 未完成（串口占用） |
| 手机离线扫码认领、Wi-Fi/云配置、首次对话 | 未完成 |
