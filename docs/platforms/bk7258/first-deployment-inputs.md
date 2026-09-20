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
| 唤醒应答资产 | `app/bk7258/assets/wake_reply.pcm`（31,208 B，SHA256 `772a8aa9…`） | AIDK AP ROMFS（板级 Kconfig 默认 `n`，AIDK preset 当前显式 `=y`，提交 `019a449e`） | AP 镜像 ROMFS | 比赛期入库，赛后删除并回退该 preset；素材缺失时构建报错 |

**已实测**：638 由干净工作树 `dc06613d` 构建（inputs 565、`dirty=false`），
operator 8,388,608 B / `33c387c1…`，实板语音全链路通过。

## 3. 公开资源（可随仓库分发）

| 资源 | 文件 / 大小 / SHA256 | 生成或消费 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| 内置唤醒模型（32 通道 INT8 DS-CNN） | `app/bk7258/models/nihao_openvela.tflite`，23,640 B，`922eba9175fcda60…` | 板级 CMake 配置期比对 `CONFIG_BK7258_VOICE_KWS_MODEL_SHA256`；运行时 `bk7258_agent_trigger.c` 重算 | Media ROMFS `/etc/media/` | KWS 就绪、`wake ready=1`；评委不需要训练环境 |
| 眼睛素材源 | `app/bk7258/assets/display/shaniu-cyan-v2.json`（+ `shaniu-cyan-v2.png` 1,048,307 B）；`shaniu-default-v1.json` 为早期版本 | `bk7258.py package eye-pack` → `.bkep` | 手机导入 → BLE 描述 + 手机 HTTPS 供包 → AIDK 板载 SD NAND | 设备回读 `pack_id`/`revision`/`source_sha256`；638 实机为 `pack=shaniu-cyan-v2 revision=2` |
| App 唤醒模型包 | `android/shaniu-companion/app/src/main/assets/wake-models/`（见下表） | App 解析 WKM1（头 136 B、模型 ≤ 65,536 B、label `[a-z0-9_]{1,31}`、phrase ≤ 63 B、模型 SHA256 必须匹配头内摘要） | 手机导入 → 设备活跃模型区 | 设备回读 active 模型 SHA256/label/phrase；改标签不等于重训 |

**三份内置唤醒模型包（2026-09-20 逐项核对，全部通过 App 解析器校验）**：

| 文件 | 大小 / 文件 SHA256 | label | phrase | 包内模型 SHA256 |
| --- | --- | --- | --- | --- |
| `nihao_openvela.wkm` | 23,776 B / `b08a256178c0b15af1191a088385501ab652446f680f7e851a076075aafd5814` | `nihao_openvela` | `你好，openvela` | `922eba9175fcda60…`（与固件内置 32 通道模型一致） |
| `nihao_bingbing.wkm` | 23,776 B / `20345f85b58ffccdf0e3d09cd3f0e3a3549c49c92bc2bcd665e7ef8c2e4345ba` | `nihao_bingbing` | `你好冰冰` | `2ced56715079b8dc…` |
| `nihao_shaniu.wkm` | 23,776 B / `d363c8253f18f3303e24f34e20accb415df04869b7f6c7cec4249ac7e4228045` | `nihao_shaniu` | `你好傻妞` | `2ade86dd6e203deb…` |

核对方式：按 `WakeModelPackage.kt` 的规则逐项验证（文件长度=136+模型长度、magic
`WKM1`、模型长度 1..65,536、头内模型 SHA256=实际模型摘要、label/phrase 的
NUL 终止与字符集），三份均为 0 failures。评审主线使用 `nihao_openvela`。

**传输与生效实现事实（按当前代码）**：唤醒模型走 BLE 控制通道的分片事务
（`MainActivity.kt` 的 `CONFIG_BEGIN` / `CONFIG_APPEND`，进度按
`uploadedBytes/totalBytes/appendCount` 显示），不是眼睛包的手机 HTTPS 供包；
固件侧由 `app/bk7258/bk7258_voice_wake_package.c` 保存 WKM1 资产与 WKA1 选择记录
（受保护 CP store），`bk7258_agent_trigger.c` 使用同一记录。设备回读为 `WKS1`
状态记录（`state`/`error`/`active`/`previous` 描述符，见 `WakeModelPackage.kt`
的 `status` 解析），因此生效判据以 active 描述符的 SHA256/label/phrase 为准。
| Android APK | `android/shaniu-companion/`（JDK 17、Android SDK 35），源码版本 `0.5.23-shaniu-rebind` / code 28 | 评委自建或用已发布 APK | 手机 | 安装后能扫描并认领设备 |

**已生成的正式眼睛包（2026-09-20 实测）**：由 `shaniu-cyan-v2.json` 生成
`shaniu-cyan-v2.bkep`，108,634 B，SHA256
`050f1175dc4b7305836eecba4ba14faac0e0210f13e3933ff4638b58176ebe79`；
`package eye-pack` 与 `verify eye-pack` 均 PASS
（`id=shaniu-cyan-v2 revision=2 entries=18`），并逐项通过 App `EyePack.kt`
的 13 项结构校验（magic `SHNEYE1\0`、version 1、头 128 B、瓦片 64/160/160、
`pack_id` 字符集、TOC 与载荷 CRC32、声明长度=文件长度），解析
`source_sha256=9a161ad6f5ae7adf011ec1be02992339ec90555084518d46dcd71edfc5775da5`。
第二个可安装包（用于演示“换包/更新”）：`shaniu-default-v1`，10,494 B，
SHA256 `1bfa445365f6081d889fd604b0dadd3d1821d87fd7ee7548259acf47fb666395`
（`entries=11`），由 `app/bk7258/assets/display/shaniu-default-v1.json` 生成。

**格式化过 SD 之后的验证循环**：在刚被 PC 格式化过的卡上，先做一次
“安装一个包 → 复位 → `读取当前眼睛`”再信任它；641 实测中，格式化后的第一轮
“安装 → 立即复位”曾出现包与 active 标记双双消失（解析器退回空卡等待），
再次安装并经一次 `usbmode msc`/安全弹出/`usbmode cdc` 后，复位保持正常
（`fallback=0`）。怀疑与“FAT 卸载不刷缓冲 + 卡内写缓存”或“首次写入的 FAT 副本
未收敛”有关，未做投机式代码改动。

**同一 `pack_id` 不能重复安装**：设备按 `<pack_id>.bkep` 存放，目标文件已存在时
`bkdisplay_store_install()` 返回 `-EEXIST`（App 显示 `-17`）。639 实机已装
`shaniu-cyan-v2`（revision 2）并激活，再装同一包即返回 -17，属预期行为；需要
演示安装时改用不同 `pack_id` 的包，或用 `读取当前眼睛` 核对当前生效包。
当前 App 没有删除/回退按钮，也不要用格式化 SD 卡解决。

按 `app/bk7258/assets/display/README.md` 的约定，生成的 `.bkep` 不入 Git；
若需要“可下载文件”形式，列为**待发布**（Release 资产）项，尚未上传。

## 4. 逐设备私有输入（每台一份，不进 Git / Release / ZIP）

| 输入 | 来源 / 生成命令 | 消费者 | 安装位置 | 判据 |
| --- | --- | --- | --- | --- |
| 设备 TLS 证书 + 私钥（EC P-256，`serverAuth` + `digitalSignature`） | `openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -sha256 -nodes -days 3650 -subj '/CN=shaniu-device' -addext 'basicConstraints=critical,CA:FALSE' -addext 'keyUsage=critical,digitalSignature' -addext 'extendedKeyUsage=serverAuth' …`（完整命令见根 README） | 设备 BLE TLS 服务端身份 | 设备 `/cpdata/shaniu/identity`（BPI1 记录，经 store 事务提交）；主机侧保留私有副本 | 设备 `bkprov status` = `identity=present`，重启后仍 present |
| 认领秘密 `possession_secret`（32 B 随机，标准 Base64） | `voice pairing --direct-cloud` 生成，与授权文件同一次 | 设备认领证明 + App 认领 | 写入授权文件字段与设备 BPI1 记录 | 设备与手机使用同一秘密，不重复生成 |
| `owner-bootstrap.json`（`provision-bootstrap-v1`） | 同上命令输出（O_EXCL、0600、fsync） | App “导入认领授权” | 手机私有目录（不进公共 Release） | 四字段：`protocol`/`device_id`/`certificate_sha256`（叶证书 DER SHA256）/`possession_secret` |
| App 控制凭据（认领后） | App 与设备协商后由 Android Keystore 保存 | App 控制会话 | 手机 Keystore | 设置回读成功；换手机/删绑定后需重新认领 |

**评委侧取得方式**：眼睛包从仓库源文件用 `package eye-pack` 一条命令生成（无
额外依赖，实测 108,634 B / `050f1175…`），无需作者的 `.bkep`；认证文件用本板
自建的 EC P-256 证书/私钥 + `voice pairing --direct-cloud` 生成授权文件并写入
设备，无需作者的证书或授权 JSON。设备端对已存在的不同身份返回 `EEXIST`、不覆盖，
且当前没有受支持的清空身份入口：已被他人认领的板必须由原所有者提供其
`owner-bootstrap.json`。

**供应通道现状**：`voice pairing --direct-cloud` → CP 控制台 `bkprov supply` →
（`bkprov-v1` RPMsg）→ AP `bkprov_storage_identity_install` → CP LittleFS
`…/shaniu/identity`。CP 命令与 AP 服务于 2026-09-20 加入（本地提交
`2c3b1906`），AIDK CP/AP 构建通过（AP 镜像含 `bkprov-v1` 端点，CP 镜像含
`BKPROV SUPPLY READY` 帧）。实板状态（AIDK，固件 `18.6.401+641`）：只读
`bkprov status` 路径已验证（`identity=present bytes=628`，复位后仍在）；
**写入尚未通过** —— 同一身份重放完成 `READY` 与全部 `NEXT offset` 后在 commit
阶段被拒（`BKPROV SUPPLY FAIL ret=-2002`，1 秒内返回、可重复），详见
[641 实板记录](../../verification/bk7258/2026-09-20-shaniu-641-full-image.md)。
作为对照，App 侧“清除认证 → 重新认领”在同一台 641 设备上成功（用户执行），
即已认领设备的产品链路不依赖 CLI 重新供应身份。
串口支持范围：当前传输经 Windows PowerShell 打开 `COMn`（WSL 通过
`powershell.exe`）；不宣称支持原生 Linux `/dev/tty*`，供应时须先关闭其他串口占用者。

**主机核对（2026-09-20，13 项全通过）**：用临时 EC P-256 证书/私钥调用
`voice pairing --direct-cloud` 并截获实际发送记录，验证 BPI1 头
（magic/version/reserved/长度字段）、叶证书 DER 与 PKCS#8 私钥 DER 逐字节匹配、
`owner-bootstrap.json` 恰为四字段、`certificate_sha256` 等于叶证书 DER 的 SHA256、
`possession_secret` 为 32 字节且与 BPI1 内秘密一致、`--resume` 复用同一记录与
同一授权文件（不轮换）。示例记录 614 B（证书 DER 428 B + 私钥 DER 138 B），
远小于 AP 侧 8,192 B 上限。设备侧写入与重启加载仍待新板实测。

## 5. 网络与云（评委自备）

| 输入 | 现状 | 判据 |
| --- | --- | --- |
| Wi-Fi（2.4 GHz） | 设备与手机同一局域网；App 经 BLE 提交 SSID/口令 | 设备回读 configured=1 且已联网 |
| 云 ASR/LLM/TTS 凭据 | 评委自己的账号/额度；App 设置页提交；设备侧 TLS 校验服务端 | 一轮完整对话（ASR→LLM→TTS→播放）成功 |
| CA 与设备时间 | 设备从网络校时（NTP）；证书有效期校验依赖手机与设备时钟 | 握手失败时先核对时间与网络 |
| 无网能力 | 本地唤醒与“我在”应答不依赖云端；完整对话必须联网 | 断网时只声明本地部分 |

## 6. 首次存储初始化（/data）：**已有入口，未在新板实测**

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

结论与现役入口：曾在当前布局下运行过、或出厂即带 LittleFS 的板可走“同板
readback → `accept-base` → 签名整包 → 烧录”路径；`persistent_data` 不是
LittleFS 的板使用 CP 命令显式初始化：

```text
nsh> bkdata status
BKDATA STATUS data=mounted type=0a732923 littlefs=yes
nsh> bkdata init --confirm erase-non-littlefs
BKDATA INIT PASS target=/data type=0a732923
```

- 语义：底层为 pinned NuttX LittleFS 的 `-o autoformat`，**只在内容不是有效
  LittleFS 时格式化**；已存在的 LittleFS（不论来源）原样挂载，绝不重写；
- 目标只有片上 FTL 块设备 `/dev/mtdblock0`；SD NAND、校准/MAC 与不可写尾区
  不受影响；命令不会在启动时自动执行；
- 判据：命令 PASS 后重启出现 `BK7258 FINALINIT PASS`，随后
  `bkprov status` 从 `identity=absent` 开始写入本机身份；
- 状态：命令已随 CP 固件构建并在镜像中（`BKDATA INIT/SKIP/FAIL` 帧），
  **尚未在新板实测**；实测前不把新板首装写成已完成。

## 7. 两阶段首装（真实顺序）

1. **阶段 A（数据与身份）**：同板 readback → `package accept-base` →
   `release full`（或 `package materialize`）生成整包 → 烧录 → 启动后
   `bkprov status` 与 `voice pairing --direct-cloud` 写入本机身份。
2. **阶段 B（产品与认领）**：安装 App → 导入 `owner-bootstrap.json` → BLE 认领 →
   配网与云配置 → 首次交互。

一阶段全量烧录不等于完成两件事：整包只恢复**已存在**的数据与身份状态。

## 8. 未闭合项与待发布动作

- 未实测：`bkdata init`（非 LittleFS 新板首次初始化）；`bkprov supply` 的
  **commit 阶段**在 AIDK/641 上被拒（`ret=-2002`，传输路径已实测、成功安装
  待定因，见 [641 实板记录](../../verification/bk7258/2026-09-20-shaniu-641-full-image.md)）；
  App 端“导入唤醒词模型 / 导入眼睛素材包 / 通过 Wi-Fi 安装所选眼睛 /
  读取当前眼睛”四步在新板的完整回读。
- 未重新生成：比赛材料 ZIP 内 PDF/DOCX/PPT（仍为 637/638 之前版本）。
- 待发布：本清单、README 首装章节与 `bkprov` 固件/工具改动均为本地提交，
  尚未推送任何远端。
