# 傻妞首启、认领与配置的生命周期与信任边界

本文只描述**当前实现**。它替代此前“PC 生成证书 → 串口供给身份 → 导入授权
文件”的隐含前置：那条路径保留为兼容/维修入口，但不再是新设备的主路径。

## 1. 生命周期

| 阶段 | 谁建立 | 落在哪里 | 允许的下一步 |
| --- | --- | --- | --- |
| `EMPTY`（无有效记录） | 无（出厂即此状态） | `usr_config` 前两个擦除扇区无有效记录 | 只允许普通挂载，不初始化 |
| `PENDING` | **正式工厂部署**：CP 命令 `bkfactory begin --transaction <32hex> --confirm factory-init` | 同上的闪存事务记录（A/B 槽 + 单调 generation + CRC32） | 仅本次首启允许初始化用户存储 |
| `STORAGE_READY` | 设备自身：`rc.sysinit` 调 `bkfactory mount`，仅在 `PENDING` 时用冻结的 LittleFS `autoformat` 初始化 `/data` | 记录状态；`/data/shaniu/factory-state` 镜像 | AP 可生成一次身份 |
| `IDENTITY_READY` | 设备自身：AP 用 mbedtls 现场生成 EC P-256 密钥与自签叶证书，组装 BPI1 记录并经受保护存储事务提交 | `/cpdata/shaniu/identity` | 可开认领窗口 |
| `UNCLAIMED_READY` | 设备自身：无 `config.bin` 且有身份 | 同上 | 显示屏二维码 + BLE 认领 |
| `DEPLOYED` | 设备自身：已有身份且有已提交配置 | 同上 | 普通重启/OTA 保留一切 |

状态由 CP 在每次启动时用**可观察事实**（`/data` 挂载类型、身份文件、配置文件）
向前推进；除部署之外没有任何回退路径。

## 2. 信任边界

- **工厂事务记录**：位于 `boards/bk7258/common/src/bk7258_storage_config.c` 声明的
  `factory_record` 区域（`usr_config` 前 8 KiB，两个 4 KiB 擦除扇区），
  由 `BK7258_STORAGE_GUARD_FACTORY_RECORD` 独占写权限。它不在任何可被首启
  初始化的文件系统内，因此格式化不会把授权语义一起抹掉。CRC 只用于检测
  损坏，**不是**授权凭证。
- **身份私钥**：只在设备受保护存储与内部运行内存中出现；不出现在 RPC、日志、
  二维码或 App。没有硬件安全存储证据，因此不声明“不可提取”。
- **认领码**：`SN1:<locator-11>:<指纹-43>:<窗口秘密-43>`（unpadded base64url）。
  窗口秘密每次开窗重新生成（mbedtls entropy/CTR_DRBG，失败则不开窗），
  因此旧二维码在新窗口必然失效。指纹是完整 32 字节叶证书 SHA256，不做截短。
- **认领与配置分离**：首次认领提交的是 `SCB4`（仅控制密钥）——不需要 Wi-Fi、
  热点、云账号或 API Key。Wi-Fi 与云凭据随后经**已认证控制通道**的 kind 7
  以 `SCB2` 记录提交（控制密钥认证，不携带身份秘密）。
- **兼容路径**：旧授权文件（`possession_secret` = 工厂身份秘密）仍可兑认领
  窗口（窗口同时接受它），但新设备从不导出该秘密，也不会由 App 凭空生成
  信任材料。

## 3. 恢复语义

| 场景 | 行为 |
| --- | --- |
| 首次初始化中断（掉电） | 记录停在 `PENDING`，下次启动重新初始化；LittleFS `autoformat` 只格式化非有效 LittleFS 内容 |
| 身份生成中断 | 记录停在 `STORAGE_READY`，下次启动重新生成；已提交身份绝不重生成 |
| 已完成首启后存储损坏 | 记录 ≥ `IDENTITY_READY` 且身份缺失 → 记为 `FAULT`；不格式化、不重发身份、不重新广播未认领 |
| 普通重启 / OTA | 保留身份、owner 与用户配置 |
| 工厂重新初始化 | 仅指定测试板 + 明确 `bkfactory begin` 可执行；不触碰 OTP、校准区与 `sys_rf/sys_net/easyflash` |

## 4. 明确未完成（不得写成已通过）

- 首次部署后的**实机**首启闭环（写记录 → 自主初始化 → 生成身份 → 屏幕二维码 →
  手机扫码认领 → 配置 Wi-Fi/云 → 真实语音）本轮受串口占用阻塞，见
  [验收记录](../../verification/bk7258/2026-09-21-shaniu-firstuse-claim.md)。
- 用户侧“恢复出厂”（撤销 owner、保留身份并重开认领窗口）尚无设备侧入口。
- 双屏二维码的实拍扫码距离/角度结论未采集。
