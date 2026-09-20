# 首次部署输入清单（评委 / 新板）

本文只列**输入与其取得方式**：每项给出「来源或生成命令 → 消费者 → 安装位置 →
是否秘密 / 是否逐设备唯一 → 成功判据」。主操作步骤见根 `README.md` 的
“评审快速开始”；原理与信任规则见
[构建/烧录/调试 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)。

状态标记：**已实测** = 有本仓库的实板或构建记录；**有工具未实测** = 工具存在但
本轮没有在新目标上跑过；**缺失（阻断）** = 当前没有可执行的取得/写入入口。

## 1. 主机与依赖（公开，无秘密）

| 输入 | 来源 / 生成命令 | 消费者 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| 完整 OpenVela 工作区 | `repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian -b dev-ai-contest-2026 -m contest2026_135_yongwangzhiqian.xml -g default,bk7258-sdk` + `repo sync -c -j8` | 官方 `build.sh --cmake` | 独立空目录（勿嵌套已有 Repo 工作区） | `repo manifest -r` 可导出；团队目录名保持 `contest2026_135_yongwangzhiqian` |
| Arm GNU 工具链 | `tools/bk7258/bk7258.py toolchain install` | 角色构建 | `prebuilt/`（来源与 SHA256 由 `toolchain.json` 锁定） | `toolchain verify` PASS |
| SDK bundle | `tools/bk7258/bk7258.py sdk rebuild --profile cp\|cp-aidk\|ap\|ap-aidk --source ../vendor/beken/bk_avdk_smp` | CP/AP 链接 | 工作区 SDK 目录（manifest 固定 `cb080de1655d579c7593ecf504c440997c4c137b`） | `sdk verify` PASS；AIDK 必须 `cp-aidk + ap-aidk` |
| 主机工具 | Ubuntu 22.04 + Git/Repo、Python 3、CMake、Ninja、Make | 全部步骤 | 主机 | 官方 openvela 构建环境说明 |

**已实测**：2026-09-20 的 637/638 构建与三轮独立源码构建均使用同一锁定依赖。

## 2. 固件构建、布局与签名输入

| 输入 | 来源 / 生成命令 | 消费者 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| CP/AP 角色配置 | `boards/bk7258/aidk_ai_toy/openvela.conf` → 角色 defconfig | `build --board aidk_ai_toy` | 构建树 `out/bk7258/<board>/...` | build manifest 的 `resolved_config_sha256` |
| 分区布局 | `boards/bk7258/common/partitions/bk7258/bk7258_ab_fixed_block_full_release.csv`（SHA256 `559f52be…`，identity `bk7258-559f52beaec8a54e`） | 布局生成、materialize、Beken loader | 构建树 `generated/`；不手填地址 | 布局身份与包内 `layout` 字段一致 |
| 发布策略 | `boards/bk7258/common/release/bk7258_product.release.csv`（SHA256 `c72b031e…`） | `release full` / materialize | 包内 `release_policy` | 每个分区策略齐备（`replace`/`preserve`/`device-unique`/`transactional`/`factory-init`/`immutable`） |
| 固件签名私钥（BL1 / MCUboot） | 发布者自建并保管；公开指纹 BL1 `58e384ae…`、MCUboot `4979ece7…` | `build --boot mcuboot`、`release full` | 仓库外私密目录（**非逐设备**，一条产品线一份） | 签名与校验通过；**私有**，不进仓库/日志 |
| 唤醒应答资产 | `app/bk7258/assets/wake_reply.pcm`（31,208 B，SHA256 `772a8aa9…`） | AIDK AP ROMFS（`CONFIG_BK7258_AIDK_WAKE_REPLY_PCM=y`） | AP 镜像 ROMFS | 比赛期入库，赛后删除并回退 defconfig |

**已实测**：638 由干净工作树 `dc06613d` 构建（inputs 565、`dirty=false`），
operator 8,388,608 B / `33c387c1…`，实板语音全链路通过。

## 3. 公开资源（可随仓库分发）

| 资源 | 文件 / 大小 / SHA256 | 生成或消费 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| 内置唤醒模型（32 通道 INT8 DS-CNN） | `app/bk7258/models/nihao_openvela.tflite`，23,640 B，`922eba9175fcda60…` | 板级 CMake 配置期比对 `CONFIG_BK7258_VOICE_KWS_MODEL_SHA256`；运行时 `bk7258_agent_trigger.c` 重算 | Media ROMFS `/etc/media/` | KWS 就绪、`wake ready=1`；评委不需要训练环境 |
| 眼睛素材源 | `app/bk7258/assets/display/shaniu-cyan-v2.json`（+ `shaniu-cyan-v2.png` 1,048,307 B）；`shaniu-default-v1.json` 为早期版本 | `bk7258.py package eye-pack` → `.bkep` | 手机导入 → BLE 描述 + 手机 HTTPS 供包 → AIDK 板载 SD NAND | 设备回读 `pack_id`/`revision`/`source_sha256`；638 实机为 `pack=shaniu-cyan-v2 revision=2` |
| App 唤醒模型包 | `android/shaniu-companion/app/src/main/assets/wake-models/`：`nihao_openvela.wkm` 23,776 B `b08a2561…`、`nihao_bingbing.wkm` 23,776 B `20345f85…`、`nihao_shaniu.wkm` 23,776 B `d363c825…` | App 解析 WKM1（头 136 B、模型 ≤ 65,536 B、label `[a-z0-9_]{1,31}`、phrase ≤ 63 B） | 手机导入 → 设备活跃模型区 | 设备回读 active 模型 SHA256/label/phrase；改标签不等于重训 |
| Android APK | `android/shaniu-companion/`（JDK 17、Android SDK 35），源码版本 `0.5.23-shaniu-rebind` / code 28 | 评委自建或用已发布 APK | 手机 | 安装后能扫描并认领设备 |

## 4. 逐设备私有输入（每台一份，不进 Git / Release / ZIP）

| 输入 | 来源 / 生成命令 | 消费者 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| 设备 TLS 证书 + 私钥（EC P-256，`serverAuth` + `digitalSignature`） | `openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -sha256 -nodes -days 3650 -subj '/CN=shaniu-device' -addext 'basicConstraints=critical,CA:FALSE' -addext 'keyUsage=critical,digitalSignature' -addext 'extendedKeyUsage=serverAuth' …`（完整命令见根 README） | 设备 BLE TLS 服务端身份 | 设备 `/cpdata/shaniu/identity`（BPI1 记录，经 store 事务提交）；主机侧保留私有副本 | 设备 `bkprov status` = `identity=present`，重启后仍 present |
| 认领秘密 `possession_secret`（32 B 随机，标准 Base64） | `voice pairing --direct-cloud` 生成，与授权文件同一次 | 设备认领证明 + App 认领 | 写入授权文件字段与设备 BPI1 记录 | 设备与手机使用同一秘密，不重复生成 |
| `owner-bootstrap.json`（`provision-bootstrap-v1`） | 同上命令输出（O_EXCL、0600、fsync） | App “导入认领授权” | 手机私有目录（不进公共 Release） | 四字段：`protocol`/`device_id`/`certificate_sha256`（叶证书 DER SHA256）/`possession_secret` |
| App 控制凭据（认领后） | App 与设备协商后由 Android Keystore 保存 | App 控制会话 | 手机 Keystore | 设置回读成功；换手机/删绑定后需重新认领 |

**供应通道现状**：`voice pairing --direct-cloud` → CP 控制台 `bkprov supply` →
（`bkprov-v1` RPMsg）→ AP `bkprov_storage_identity_install` → CP LittleFS
`…/shaniu/identity`。CP 命令与 AP 服务于 2026-09-20 加入（本地提交
`2c3b1906`），AIDK CP/AP 构建通过（AP 镜像含 `bkprov-v1` 端点，CP 镜像含
`BKPROV SUPPLY READY` 帧）；**尚未在新板实测安装**。
串口支持范围：当前传输经 Windows PowerShell 打开 `COMn`（WSL 通过
`powershell.exe`）；不宣称支持原生 Linux `/dev/tty*`，供应时须先关闭其他串口占用者。

## 5. 网络与云（评委自备）

| 输入 | 现状 | 判据 |
| --- | --- | --- |
| Wi-Fi（2.4 GHz） | 设备与手机同一局域网；App 经 BLE 提交 SSID/口令 | 设备回读 configured=1 且已联网 |
| 云 ASR/LLM/TTS 凭据 | 评委自己的账号/额度；App 设置页提交；设备侧 TLS 校验服务端 | 一轮完整对话（ASR→LLM→TTS→播放）成功 |
| CA 与设备时间 | 设备从网络校时（NTP）；证书有效期校验依赖手机与设备时钟 | 握手失败时先核对时间与网络 |
| 无网能力 | 本地唤醒与“我在”应答不依赖云端；完整对话必须联网 | 断网时只声明本地部分 |

## 6. 首次存储初始化（/data）：**缺失（阻断）**

现状证据：

- `boards/bk7258/aidk_ai_toy/src/etc/init.d/rc.sysinit` 只执行
  `mount -t littlefs /dev/mtdblock0 /data`；
  `boards/bk7258/common/src/bk7258_bringup.c` 明确 “rc.sysinit mounts the
  selected filesystem and **never formats it**”。
- `boards/bk7258/common/src/bk7258_finalinit.c` 在 `statfs("/data").f_type`
  不等于 LittleFS magic 时打印
  `BK7258 FINALINIT FAIL: persistent data at /data has type …, expected 0a732923`，
  启动判定不通过。
- 现有工具只能把**同板 readback** 中的 `persistent_data` 原样搬进整包
  （`package accept-base` → `release full` / `package materialize`），没有
  “把非 LittleFS 的 persistent_data 初始化成 LittleFS”的入口。

结论：**曾在当前布局下运行过、或出厂即带 LittleFS 的板**可走“同板 readback →
`accept-base` → 签名整包 → 烧录”路径；`persistent_data` 不是 LittleFS 的板
（例如被整片诊断镜像清过，或出厂分区不含 LittleFS）目前没有受支持的首装入口。
最小修复方向（本轮未实现）：设备侧、**显式请求**且仅在 `persistent_data` 被证明
不是有效 LittleFS 时才执行的初始化动作；禁止自动格式化，禁止覆盖同板校准/MAC/
身份数据。该动作落地并通过实板验证前，本清单不把新板首装写成已完成。

## 7. 两阶段首装（真实顺序）

1. **阶段 A（数据与身份）**：同板 readback → `package accept-base` →
   `release full`（或 `package materialize`）生成整包 → 烧录 → 启动后
   `bkprov status` 与 `voice pairing --direct-cloud` 写入本机身份。
2. **阶段 B（产品与认领）**：安装 App → 导入 `owner-bootstrap.json` → BLE 认领 →
   配网与云配置 → 首次交互。

一阶段全量烧录不等于完成两件事：整包只恢复**已存在**的数据与身份状态。

## 8. 未闭合项与待发布动作

- 缺失：`persistent_data` 非 LittleFS 的新板首次初始化（见第 6 节）。
- 未实测：`bkprov supply` 在真实新板上的安装与重启加载；App 端“导入唤醒词模型 /
  导入眼睛素材包 / 通过 Wi-Fi 安装所选眼睛 / 读取当前眼睛”四步在新板的完整回读。
- 未重新生成：比赛材料 ZIP 内 PDF/DOCX/PPT（仍为 637/638 之前版本）。
- 待发布：本清单、README 首装章节与 `bkprov` 固件/工具改动均为本地提交，
  尚未推送任何远端。
