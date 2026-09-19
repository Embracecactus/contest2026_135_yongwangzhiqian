# 傻妞原生 Android companion：产品与实施计划

本文是 [傻妞全项目 Master Plan](shaniu-master-plan.md) 的 Android 与微信小程序专项计划。
项目级依赖、里程碑状态和最终交付口径以 Master Plan 为准。

状态：现役范围已按 2026-09-20 更新；第 2 节起是历史方案，不是待恢复的实现清单。

当前工程为 `android/shaniu-companion`，源码 App 28 / `0.5.23-shaniu-rebind`。
App 配网、设置和实际 OTA 结果见 Master Plan 的 634 记录；635 仅补运行时 Skill，
其语音/显示已实测，App 控制与 OTA 未重测。NFC 芯片驱动已适配，但未用于当前
板端产品流程；本轮不把碰一碰列为完成项。

范围：AIDK AI Toy 与原生 Android App；日常对话直接使用手机配置的云端模型，不要求额外 Gateway。

## 1. 当前产品约束

1. K2 为电源键，K1/K3 为音量减/加；硬件不支持真断电的限制必须明确，PTT仅为内部调试。
2. 日常使用官方语音唤醒，自动收音、端点、云端回复与播放；不把PTT作为首次使用或验收前置。
3. 未认领设备受控发现，通过已有同板授权与 BLE 完成认领、Wi-Fi/云凭据配置；NFC 为后续接入。
4. App 首页下方提供固件更新，认证 BLE 传必要控制，手机临时 HTTPS 来源经 Wi-Fi 传签名固件。
5. START回执不是升级完成；需设备已确认阶段、重连后实际版本和安全计数一致。保留失败/回滚证据。
6. App 不向板端 Shell/GPIO/原始 Flash 发任意命令，不展示凭据明文回读，不删除认证保护以通过测试。
7. 当前协议与验收步骤见 [Android README](../../../android/shaniu-companion/README.md)，状态以 [Master Plan](shaniu-master-plan.md) 为准。

下列第2节起保留2026-09-02的历史基础/拓扑说明；其中 Gateway、PTT 主入口及微信小程序计划不再是本轮产品约束或必交范围。实际进展只从上述当前文档读取，不将历史方案重新启用。

## 2. 历史基础与当时未完成边界（2026-09-02）

| 能力 | 当前基础 | 仍需完成 |
|---|---|---|
| Wi-Fi | CP controller/backend、AP VNET、TCP/UDP 和现有 `bkwifi` 诊断入口 | 产品配网服务、凭据保护、自动重连和 Gateway 注册 |
| BLE | BK7258 HCI、NuttX Host/GATT 和测试服务已有基础 | 产品 `provision-v1` GATT、认领、撤销、配网安全和 AIDK 组合实板证据 |
| Voice | `companion-v1` codec/session、半双工仲裁、public-media adapter 和 capture frame pump | PTT task owner、真实 Gateway sink、TLS/WSS、流式回放和实机 turn 验收 |
| Camera | GC2145 以 `/dev/video0` 暴露 640×480/30 fps MJPEG | 稳定抓帧证据、App 单帧 owner、上传、取消、资源回收和低帧率预览 |
| Display | 两块 160×160 GC9D01 为 `/dev/fb0`、`/dev/fb1` | 物理左右校准、产品 UI、脏矩形刷新和长稳 |
| Sensors | SC7A20H、MFRC522、电池、按键、LED 和 SD NAND 已有板级入口 | App 语义事件、权限、组合资源策略和实板产品验收 |
| Motion | 当前硬件声明没有已接入电机 | 首版不承诺任何物理运动能力 |

构建通过、设备注册和历史单项证据都不能替代当前完整产品 profile 的实板闭环。

## 3. 历史系统拓扑（非当前产品路径）

```text
首次认领：
Android App -- BLE provision-v1 --> AIDK

正常运行：
Android App -- HTTPS/WSS console-v1 --> User Gateway
                                             |
                               TLS/WSS companion-v1
                                             |
                                            AIDK
                                             |
                    capability/policy/turn-state arbiter
                                             |
             audio | dual LCD | camera | motion | NFC | battery

后续轻客户端：
微信小程序 -- HTTPS/WSS console-v1 --> User Gateway
```

正常运行不接受从公网直接连接板端。远程访问由 Gateway 的认证、撤销和审计入口承接；若未来
增加同局域网直连，只能作为独立受测 profile，不能成为配网和远程访问的隐式旁路。

## 4. 控制模型：外设不是大模型的裸手脚

大模型只产生有限的语义请求：

```text
LLM/tool request
  -> App capability allowlist
  -> user permission and request expiry
  -> product state/resource arbitration
  -> public NuttX/OpenVela API
  -> Chip lower-half
  -> Board physical binding
```

首版允许的代表性能力：

- `audio.speak`、`display.emotion`、`vision.snapshot`；
- `motion.read_state`、`nfc.read_tag`、`battery.read`；
- `session.cancel`、`asset.play_signed`。

管理员能力只能从已认领 Android App 发起：

- `network.provision`、`gateway.bind`、`firmware.update`；
- `permission.configure`、`memory.delete`、`device.unbind`。

永不暴露：任意 GPIO/寄存器访问、Shell、裸 Flash 写入、密钥读取、关闭真实 MIC/Camera 指示，
以及不受限的远程脚本或 prompt 注入。

## 5. Android 首版页面

### 5.1 添加设备

- 扫描附近未认领的傻妞；
- 显示设备短 ID，并要求设备侧按键或双屏确认；
- 通过 `provision-v1` 配置 Wi-Fi 和 Gateway；
- 显示关联、DHCP、Gateway 注册的分阶段结果；
- 支持取消、错误密码重试、解绑和恢复流程。

### 5.2 首页

- 在线/离线、Wi-Fi、Gateway、固件和资产版本；
- 电量、充电状态、存储和当前产品状态；
- MIC、Camera、位置、长期记忆和授权音色的可见状态；
- 不展示未经用户选择的原始日志或私密正文。

### 5.3 互动

- 文字输入、PTT、取消当前 turn；
- 显示 Listening/Thinking/Speaking/Offline，而不是伪造在线成功；
- 支持离线固定语音包，但动态回答失败时明确降级。

### 5.4 视觉

- 首版只提供显式单帧抓取与 VLM 结果；
- 第二阶段提供用户持续确认期间的 2–5 fps MJPEG 预览；
- App 退后台、锁屏、断网、超时或用户松开控制后立即停止；
- 原图默认不持久化，用户明确保存时才写入手机私有目录。

### 5.5 个性与心情

必须区分三类状态：

| 类型 | 示例 | 能否由手机覆盖 |
|---|---|---|
| `system_state` | listening、camera、offline、low-battery、error | 否 |
| `emotion` | happy、shy、sad、surprised、thinking | 只允许产品状态机短期选择 |
| `persona_mode` | gentle、playful、quiet、serious、tsundere-lite | 是，具有会话/定时/长期有效期 |

手机修改的是有限的 `persona_mode`，不是任意系统 prompt；安全、错误、隐私和合成身份披露
始终具有更高优先级。

### 5.6 隐私与设备管理

- 分别授权和撤销 MIC、Camera、位置、长期记忆与授权音色；
- 管理音量、安静时间、Gateway、OTA、签名资产和设备解绑；
- 只显示不含内容的诊断摘要、失败码、计数和版本；
- 删除操作需要明确对象、影响范围和确认，不能由 LLM 发起。

## 6. 权限等级

| 等级 | 能力 | 首版要求 |
|---|---|---|
| L0 | 电量、网络、版本、只读状态 | 已认领用户可读 |
| L1 | 音量、显示偏好、persona mode | 可撤销、可过期 |
| L2 | MIC、Camera、位置、远程预览 | 设备可见指示；必要时本地物理确认 |
| L3 | Wi-Fi、Gateway、OTA、删除、解绑 | 重新认证并要求设备本地确认 |
| Forbidden | Shell、GPIO、密钥、裸 Flash、关闭隐私指示 | 所有客户端永久禁止 |

服务端和板端都执行权限与过期检查，不能只信任 Android UI 已隐藏按钮。

## 7. 三条协议

### 7.1 BLE `provision-v1`

认证通道、bootstrap、物理确认和凭据存储边界见
[provision-v1 安全决策](shaniu-provision-security.md)。三端实现和主机协议测试已接入；
真实 Android 手机与 AIDK 的发现、认领、重启恢复和解绑仍是独立物理验收门。

最小状态流为：

```text
DISCOVER -> CLAIM_CHALLENGE -> LOCAL_CONFIRM -> SECURE_CONFIG
         -> APPLY -> WIFI_RESULT -> GATEWAY_RESULT -> COMMIT/ROLLBACK
```

协议至少携带版本、事务 ID、分片序号、长度、超时和结果码。凭据不能写入日志或普通通知；
在完成独立安全 ADR、确定 possession proof、会话密钥和 software-backed key storage 边界之前，
不得用自创“异或/固定 key”方案发送 Wi-Fi 密码。

### 7.2 Gateway `console-v1`

- HTTPS 处理认领、设置、权限、资产和管理操作；
- WSS 推送 presence、battery、turn、vision、update 和 error 事件；
- 每个 mutation 带 request ID、设备 generation、期望旧版本和有效期；
- Gateway 保存 desired state，设备回报 reported state，二者不一致必须可见；
- Android 和小程序复用同一权限、schema 和撤销机制。

### 7.3 Device `companion-v1`

- 继续承载板端与 Gateway 的 PCM/JPEG 及实时控制；
- 手机不重新实现该二进制设备协议；
- 视频首版沿用有界 JPEG chunk、窗口和背压，由 Gateway 转发成手机消费的会话；
- 不在 BLE、MQTT 或日志中搬运连续音频和图片。

## 8. Android 工程边界

首版采用原生 Kotlin。最低 Android 版本和 target SDK 在登记实际测试手机后冻结，不能先写死
再跳过老版本权限分支。工程按职责分离：

```text
app/ui                 Compose 页面、导航和可访问性
domain                 device/session/permission/mood 状态机
device/ble             扫描、认领、provision-v1、断连恢复
gateway                HTTPS/WSS console-v1、认证和重连
media                  JPEG snapshot、MJPEG buffer/decoder、保存授权
security               Android Keystore token、证书固定策略和日志清理
test-support           fake BLE、fake Gateway、协议向量和故障注入
```

Android 只保存设备引用、Gateway token handle 和非敏感偏好。Wi-Fi 密码完成配网后默认不保留；
token 使用 Android Keystore 保护，截图、崩溃报告和调试日志不得包含凭据、原始对话或图片。

## 9. 实施阶段与验收门

### A0：协议与 Android 空壳

- 冻结 `provision-v1`/`console-v1` schema、错误码和测试向量；
- 建立 Kotlin 工程、页面导航、fake device 与 fake Gateway；
- 单元测试覆盖状态恢复、重复事件、过期请求和权限拒绝。

### A1：BLE 认领与配网

- AIDK 产品 GATT 和 Android `device/ble` 对接；
- 20 次发现/认领/配网/重启重连通过；
- 错误密码、断连、重复提交、取消和撤销均 fail closed；
- 串口、Android 日志和抓包证据中没有明文凭据。

### A2：Gateway 控制台

- 首页状态、权限、音量、persona mode 和取消 turn；
- 弱网、Gateway 重启、token 撤销和设备 generation 变化可恢复；
- Android 不在线时 AIDK 仍能进入离线模式并使用本地资产。

### A3：单帧视觉

- 手机显式请求、设备本地指示、单帧抓取、VLM 回答和释放完整闭环；
- 连续 100 次成功/取消/超时后无 fd、heap 或 camera owner 泄漏；
- 原图不默认落盘，用户保存和删除行为可验证。

### A4：低帧率视频

- 固定从 640×480 MJPEG、2–5 fps、单 viewer 起步；
- 30 分钟预览无无界队列、heap 增长或隐私指示错位；
- App 后台、锁屏、断线、Gateway 背压和设备低电均能终止会话；
- 通过音频并发测试前，视频与语音 turn 保持互斥。

### A5：管理与交付

- OTA、签名资产、解绑、恢复、隐私导出/删除和诊断摘要；
- 升级失败可回滚，不能损坏 `sys_rf`、设备身份或最后一个可启动镜像；
- 发布 APK 记录版本、签名、hash、兼容设备和权限用途。

## 10. 后续微信小程序边界

小程序只复用 Gateway `console-v1`，首批包含：

- 账号登录和已绑定设备列表；
- 在线、电量、版本和会话状态；
- 文字对话、取消、音量和 persona mode；
- 用户明确请求的单张图片；
- 权限状态查看和远程撤销。

首批明确不包含 BLE 首次配网、持续 MJPEG、后台长连接保证、OTA 文件、底层诊断和恢复出厂。
若未来平台能力和主体资质满足，也必须另做安全、生命周期和视频协议验收，不能因为 Android
已经通过就自动继承结论。

## 11. 与板端路线的先后关系

执行顺序统一见[主计划功能完善批次](shaniu-master-plan.md#feature-completion-execution)。
复用已有客户端，先在受控配置下联调真实 Gateway 状态、取消及设置，再完成 BLE 首次认领
和配网；不能让 BLE 全部完成成为第一次 App 联调的前置条件。正式交付仍必须通过 A1 的
认领/凭据门。单帧视觉先于预览，OTA 与模型/记忆分别验收。

任何阶段的 host test、Android emulator、构建或 Gateway mock 都不是实板产品验收。
