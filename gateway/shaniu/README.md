<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞 companion-v1 Gateway

> 冻结快照（2026-09-09）：本目录保留独立 Python Gateway 的现有实现，供历史恢复、
> 协议参考和开发测试使用。后续产品主线改为板端直接接入 MiMo，不要求额外部署
> Gateway，也不默认将其迁移为手机常驻服务。IndexTTS 2.5 私有声音模型作为并行
> 路线保留，本快照不包含模型或真实凭据。
>
> 快照包含 MiMo 适配、console-v1、设备绑定、长期记忆及 OTA 内容与事务模块。
> 主机测试不代表完整实板验收；尤其 OTA 断电恢复与端到端升级仍未闭环。
> 本次归档范围仅为 `gateway/shaniu`，不包含工作区中尚未提交的固件和 Android 改动。

服务复用 `companion-v1` 的 WSS、状态机、双向窗口和流式下行。默认使用确定性音调夹具；
显式选择 `--provider mimo` 后走“松键 PCM → WAV/ASR → 对话 → 流式 TTS → 板端 PCM”。
默认监听桌面 loopback；受控网络接入需双向 TLS 认证和操作员预登记证书绑定；
App/BLE 认领和私有音色尚未接入。可选 console-v1 HTTPS/WSS 端点支持连接状态、人物设置
和显式选择设备的长期记忆清除/撤销。

## 当前行为

- 语音入口只接受 WSS 的 `/companion/v1`，并要求 WebSocket subprotocol `companion-v1`；
- TLS 最低版本为 1.2，不提供明文 `ws://` 或跳过证书校验的服务端选项；
- 默认仍只绑定 loopback，且不要求客户端证书。绑定非 loopback 地址必须同时提供
  `--client-ca` 和 `--device-bindings`：服务端既验证 TLS 客户端证书由该 CA
  签发，也要求叶证书指纹已登记到唯一设备 ID；
- 逐帧校验 40-byte network-order header、身份、turn、sequence、payload、状态和
  `WINDOW_UPDATE`；
- WSS 映射固定为“一条 binary WebSocket message 承载一个完整 `companion-v1` frame”，
  不接受 text message、半帧或一条 message 内拼接多帧；
- 固定回复模式在 `TURN_END` 后先发 `TTS_START`，再按下行窗口逐个生成并发送 20 ms、16 kHz、mono、
  S16LE 的确定性音调帧，最后发 `TTS_END`；不构造或缓存整句 WAV；
- 板端只有在完整下行帧被本地 DAC 队列接受后才返还等量窗口；Gateway 在额度不足时等待，
  不依靠一次性初始额度容纳整段回答；
- 日志只包含状态、帧/字节计数、首包延迟和错误码，不记录 PCM、文本、会话 ID 或
  远端地址。

## 本地运行

### 受控设备身份绑定

在原 Gateway 启动参数中增加 `--device-bindings <registry.json>`，并提供
`--client-ca`。启用绑定后即使监听 loopback 也要求 mTLS；CA 验证和叶证书 DER
SHA-256 登记必须同时通过。服务端不采用 HELLO payload、URL 或连接顺序作为设备 ID。

登记文件格式如下，散列占位值需要替换为实际已供应客户端证书的 DER SHA-256：

```json
{"format":"shaniu.device-bindings/1","devices":[
  {"device_id":"aidk-1","client_certificate_sha256":"0000000000000000000000000000000000000000000000000000000000000000"}
]}
```

文件最多 64 KiB/256 个设备，须为当前操作员拥有的普通文件，不能由组或其他用户写入，
不接受最终路径为符号链接。设备 ID 和证书各自唯一；未知字段和重复 JSON 键会报错。
此文件不包含私钥或 App bearer token，也不替代用户认领流程。证书轮换需更新登记并
重启服务；当前没有热更新或自动认领。

一个设备同时只保留一个连接，重复连接被拒绝；HELLO 完成后才可由内部控制层找到在线
连接。首帧等待默认上限 5 秒，超时清理设备占用。关闭后允许重新连接，日志不打印 ID
或证书指纹。未提供登记文件时只保留 loopback 语音夹具，不建立可供控制层寻址的设备。

### Android console-v1 控制入口

在已配置 MiMo、mTLS 设备登记的同一 Gateway 命令上增加：

```sh
--console-port 8770 --console-access <access.json> --console-state <private-state-dir>/console.sqlite
```

长期记忆默认关闭。仅在同时启用 MiMo、console 和设备证书绑定时，操作员才能为明确列出的
设备启用私有文本记忆：

```sh
--memory-state <private-state-dir>/memory.sqlite --memory-device aidk-1
```

省略这两个选项时不创建记忆库，也不跨连接保存对话。可重复 `--memory-device` 选择多个已
登记设备；未列出的设备保持 `not_granted`。记忆库要求当前操作员拥有的 0600 普通文件并
拒绝符号链接；父目录也须由当前操作员拥有且不可由组或其他用户写入，路径中拒绝不可信
目录和符号链接。每设备最多保留 32 条、默认 30 天；每次仅取最近 4 个完整问答且总计不
超过 8192 字符。只保存转写与回答文本，不保存 PCM、TTS 音频、凭据或私有音色。

控制端口须与语音端口不同。Android origin 填控制端口的 HTTPS 地址，并使用该 App
信任的服务器证书；语音连接继续要求板端 mTLS，控制端口使用服务器 TLS + Bearer 授权，
不要求手机持有板端客户端私钥。默认不开启控制监听，未启动额外公网服务。
console 写请求接受协议媒体类型 `application/vnd.shaniu.console-v1+json`；同时保留
`application/json` 供现有受控客户端兼容，其他媒体类型拒绝。

授权文件是操作员预供应的 grant，不是 App 自助认领。格式为：

```json
{"format":"shaniu.console-access/1","grants":[
  {"token_sha256":"0000000000000000000000000000000000000000000000000000000000000000",
   "device_id":"aidk-1","write":true,"expires_at_ms":1800000000000}
]}
```

替换散列与过期时间占位值；散列来自独立随机控制令牌的原始 ASCII 字节，不包含
`Bearer ` 前缀或换行。不要复用 MiMo API key。原始令牌只供应 Android 的加密凭据存储，
Gateway 文件保存 SHA-256；`write:false` 仅允许读状态。grant 只能指向已登记设备，
文件最多 256 个授权、64 KiB，须由当前操作员拥有且不可由其他用户写入。
授权/证书更新目前通过重启服务生效，事件连接也会检查过期时间。

`console.sqlite` 的父目录须已存在，文件要求当前操作员私有权限。它保存单调增加的
控制代次，防止重启或板端重连后接受旧请求；不要把它当可随意清空的临时缓存。
人物设置由 `console.sqlite` 按设备持久化；新板端连接空闲时恢复到 MiMo conversation；
若重连时回合仍在处理或等待播放确认，快照先报告真实运行期人物，空闲后再恢复。
写库失败则回滚本次运行期切换并返回服务不可用。它不把 persona 写入板端。

| 端点 | 当前行为 |
| --- | --- |
| `GET /console/v1/devices/{id}/snapshot` | 已完成 HELLO 的绑定设备连接状态；离线返回 503 |
| `GET /console/v1/devices/{id}/firmware/releases?generation=…` | 只投影已验签、绑定该设备且来源版本与板端报告一致的 OTA 元数据；不下载或安装 |
| `POST /console/v1/devices/{id}/mutations` | 支持 `persona_mode.set`、`volume.set`、`turn.cancel`、受登记约束的 `firmware.update`，以及已启用记忆的 `memory.delete`/单向撤销；校验代次、修订、期限和同 ID 重试 |
| `WSS /console/v1/devices/{id}/events?generation=…&after_sequence=…` | 状态变化时推送完整快照，断线/换代/授权到期关闭 |

请求有效期最多 60 秒，每个设备代次最多缓存 128 个未过期回执，只保留请求摘要。
人物切换在回合间执行；进行中返回 409。`turn.cancel` 使用空 `arguments`；`revision`
同时随新语音轮次推进，旧快照取消返回 `revision_conflict`。每设备串行处理写请求，
相同 ID 并发重试只发送一次。取消会停止 provider、清空本轮 PCM 并发送板端 CANCEL；
accepted 仅表示取消已发送，不是板端停止确认。随后快照显示 `cancelling`，直到收到
同一轮的 companion-v1 ACK 才回到 idle；普通 session ACK 不确认取消。
板端仅在串行会话清理成功、采集线程退出且回合空闲后发此 ACK。旧固件未实现此回执时
先保持等待，5 秒未确认则显示 `cancel_unconfirmed`（停止结果未知），不推断停止成功；
迟到的同轮 ACK 仍可确认，新轮开始会替换旧轮的等待状态。未开始过回合时返回 409。
`volume.set` 在板端声明能力时发送控制，等待对应请求序号的回报后才返回 accepted；
未支持、忙、超时及板端错误分别返回 volume_not_supported、volume_busy、volume_timeout、
volume_device_error。超时表示生效结果未知，重复同一请求 ID 复用原回执、不重新发送。
快照在音量未知且设备支持时尝试查询；等待、错误和超时保持 null，成功显示策略读回值。
音量只在运行期跨轮次保留，未写入偏好数据库。未知操作返回 unsupported_operation。
可选 `--firmware-releases <registry.json>` 仅能与 console 一起启用。登记文件通过
`bk7258.py release gateway-catalog` 从已密码学验证的 device-bound product delivery
生成，最多 32 项；Gateway 启动时要求它为当前操作员拥有、不可由组或其他用户写入的
普通非软链接文件。服务不会从登记文件读取 URL/路径；只有显式提供受控包文件时才会读取 OTA 包。板端尚未报告
合法当前版本和 MCUboot 公钥根指纹时返回空列表，不以登记表猜测设备版本或信任根；
只有源版本与公钥根 SHA-256 都匹配的登记项才会进入兼容列表。
声明状态上报能力的新固件从 AP health worker 的非阻塞缓存提供充电状态和原始电压；
console 目前投影充电状态。电芯曲线尚未标定，电量百分比保持 null；AP 从当前运行
MCUboot 镜像头读取固件版本，并随状态帧上报嵌入的 canonical DER 公钥根 SHA-256。
OTA 通过 HELLO capability `CAP_OTA=1<<3` 显式声明。Gateway 仅在设备、已观察来源版本和
根指纹唯一匹配 operator release、且语音/播放空闲时发送 `OTA_REQUEST(21)`：flags=0、turn=0，
payload 是恰好 32 字节的 manifest SHA-256。板端以 `OTA_REPORT(22)` 回报
`!IiBBH32s`（request sequence、signed result、phase、progress、reserved、manifest）；
phase 依次为 downloading、verifying、staged、rebooting、trial、confirmed，或终态
rolled_back/failed。正常阶段只能前进，同阶段进度不能回退；终态冻结，迟到报告不改写状态。
Gateway 只接受当前请求 sequence 和 digest 都匹配的报告。首个成功报告才令 mutation accepted；
`failed/result<0`、发送后超时或断线均返回 `update_device_error` 并使 revision 失效，保留受验证
release 以投影迟到的匹配报告。请求中永不包含 URL、路径、包或 CA。

若要让已接受、尚未终态的 OTA 实际取得内容，操作员还必须在同一启动命令中对每个登记包
重复提供 `--firmware-package <signed-ota.bkpack>`。启动时 Gateway 只接受当前用户拥有、
不可被组/其他用户写入的非软链接普通文件，最多 64 MiB；拒绝压缩或加密 ZIP、重复成员、
非 canonical `catalog.json`、缺少 `catalog.sig`、以及与登记中 size/SHA-256/catalog 摘要
不能唯一对应的包。它只通过现有 mTLS 语音端口按 `GET /firmware/v1/{manifest}/catalog.json`、
`catalog.sig` 和 catalog 声明的精确 CP/AP URI 提供内容。访问者必须是登记的同一设备，且其
当前在线连接正持有该 manifest 的非终态 OTA 报告；未知路径、查询、包目录、其他设备和终态
请求一律不暴露内容。catalog/signature 仅接受无 Range 的 200；镜像必须使用唯一 canonical
`Range: bytes=start-end`，单次最多 16384 bytes，返回 206 和精确 `Content-Range`。每个响应均
为 `Cache-Control: no-store` 与 `X-Content-Type-Options: nosniff`。Gateway 从启动时持有的文件
描述符按 ZIP_STORED 偏移读取，板端仍独立验证 catalog 签名、镜像哈希和目标约束。
turn 在发送 TTS_END 后继续显示 speaking，收到正常播放 ACK 才回到 idle；5 秒无 ACK
显示 playback_unconfirmed，迟到 ACK 可恢复。等待期间人物切换返回 409；已确认空闲
且无待取消任务时取消也返回 409，不撤回已完成对话。除长期记忆外，隐私权限仍表示此控制
入口未授予的远程权限，不能用来推断物理 PTT 的本地授权。长期记忆只有操作员启动时显式
选择且数据库状态为 enabled 才上报 allowed；`memory.delete` 删除当前设备已保存对话但继续
保留授权，`permission.configure` 只接受将 `long_term_memory` 设为 denied，并以单一事务
清空记录和持久化撤销。远程开启被拒绝为 `local_confirmation_required`；撤销状态不会因
服务重启时再次出现 `--memory-device` 而自动恢复。

联调日志分别记录 `turns_completed`（发送 TTS_END）和 `playback_confirmed`（收到对应
待确认轮次的板端停止 ACK）。正常异步播放在清理成功后回 ACK；重复 ACK 不重复计数，
取消会清除正常播放的待确认记录。该计数证明板端软件报告完成，不替代人耳验收。

共享协议样例在 `tests/fixtures/console-v1`，同时被真实本地 HTTPS 测试和 Android
单元测试消费。服务不记录访问令牌、请求正文或设备 ID。

### 开发环境

使用 Python 3.10 或更高版本安装冻结依赖：

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

下面的证书只适合本机开发测试，不得作为产品证书或部署凭据：

```sh
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 1 \
  -subj /CN=localhost -addext subjectAltName=DNS:localhost
```

启动服务：

```sh
.venv/bin/python -m shaniu_gateway \
  --cert cert.pem --key key.pem --host 127.0.0.1 --port 8765
```

默认服务不会接受 `0.0.0.0`、LAN 或公网地址。受控 LAN 验证必须显式传入
`--client-ca <CA-file> --device-bindings <registry.json>`；此时只接受单播字面
IPv4/IPv6 绑定，仍拒绝
`0.0.0.0`、`::`、有限广播 `255.255.255.255`、组播和域名绑定，并强制客户端证书验证。
这些选项不生成部署凭据或登记记录，也不输出身份日志；证书、登记表和端点由受控部署流程提供。

## MiMo 语音对话

首期采用现有 BKVoice/AP session owner，Gateway 编排三个 MiMo 请求；没有并发启用官方
Agent 的第二套对话或音频状态机。官方框架复用与替代条件见项目主计划第 17.5 节。

- ASR 使用 `mimo-v2.5-asr`，`TURN_END` 后提交一个 16 kHz/mono/S16 WAV 的 `input_audio`；
  单次最多 30 秒（960,000 字节），空输入或超限失败关闭。不是实时上行 ASR。
- 对话使用 `mimo-v2.5`，可选 `mimo-v2.5-pro`；关闭 thinking，最多 1024 个输出 token，
  首版只返回文本、不注册工具；上下文保留最近 4 个完整问答，历史总计最多 8192 字符。
  等完整短回答后开始 TTS。
- TTS 使用 `mimo-v2.5-tts` 的 `mimo_default`，请求 `pcm16`，逐条解码 SSE Base64 音频。
  必须先验证供应商实际输出是 mono/S16LE 及其采样率，再填 `--mimo-tts-rate`；此参数没有默认值。
  非 16 kHz 通过系统 FFmpeg 的流式重采样转为 16 kHz，不写临时音频文件。
- 首个合法音频帧到达后才发 `TTS_START`；背压、取消与关闭复用现有连接 owner。
  协议要求所有音频帧固定为 640 字节，因此只将 TTS 尾帧补静音到 20 ms；不丢尾音，
  不给输入录音补零。输出最多 90 秒，整轮处理/播放最多 110 秒，每次 HTTP 请求最多 45 秒。
- 请求校验证书与 hostname，不跟随重定向、不自动重试、不读取环境代理；异常只记固定错误码。
  供应商 key 仅从当前用户持有、模式 0600 的文件读取，拒绝最终路径为软链接。
  原始录音只在本轮内存中使用；转写与回答按上述上限保留在连接内存，不进入日志或仓库。
- 每个连接通过 `reply_factory` 创建独立会话。发送 `TTS_END` 后保留待确认回答，收到
  对应播放完成 ACK 后才提交历史；重复 ACK 不重复提交。下一轮先开始而上一轮未确认时
  丢弃待确认回答，避免模型引用设备未确认播放的内容。旧固件无完成 ACK 时仍可发起
  单轮对话，但其未确认回答不会进入短期上下文。设备报告最近一轮播放错误时撤回该轮，
  生成失败或取消不提交。
  未配置长期记忆时断连清空历史；显式配置后，仅在收到对应播放完成 ACK 时才把问答写入
  当前设备的私有库，重连时恢复有界上下文。取消、生成失败、断连、未确认播放和播放错误均
  不写入。App 可通过已鉴权 console-v1 清除或单向撤销，不能远程开启。App 通过
  `persona_mode.set` 设置人物，Gateway 按设备持久化；实板听感和完整链路仍待验收。

人物风格由 `--persona-mode` 选择：`gentle`（默认）、`playful`、`quiet`、`serious`、
`tsundere_lite`。风格实际加入对话的固定 system policy，不更换音色，也不修改 emotion。
会话 owner 可在两轮之间调用 `set_persona()`；进行中、待提交或已关闭会话拒绝切换，
切换保留已有的有界历史，不影响其他连接。五种启动配置均已通过真实 MiMo 短文本请求验证，
风格质量与实板听感仍待评价。

启动示例（从本目录运行；`MIMO_TTS_RATE` 必须来自格式验证，不是猜测值）：

```sh
.venv/bin/python -m shaniu_gateway \
  --cert cert.pem --key key.pem --host 127.0.0.1 --port 8765 \
  --provider mimo --mimo-key-file /private/shaniu/mimo.key \
  --mimo-base-url https://token-plan-cn.xiaomimimo.com/v1 \
  --mimo-chat-model mimo-v2.5 --mimo-tts-rate "${MIMO_TTS_RATE:?set verified PCM rate}"
```

2026-09-07 已用用户授权账号通过真实对话、ASR、流式 TTS，以及本机客户端经生产 Gateway
的 WSS 问答。当前内置音色 WAV 头实测 24 kHz/mono/S16，可据此使用 `--mimo-tts-rate 24000`；
原始 PCM 流无采样率头，板端播放速度与听感仍待验收。该账号的接口可用不等于确认额外订阅豁免。
证据统一见项目 `docs/verification/bk7258/2026-09-07-mimo-gateway-live.md`；
尚未完成 AIDK 物理 PTT 或连续 50 轮验收。

## 验证

```sh
make test
```

测试用例在临时目录生成并销毁自签证书，实际建立 loopback WSS 连接，覆盖正常回复、
取消、sequence gap、turn 中断连、下行背压、证书 hostname 失败、绑定限制，以及
mTLS 的可信客户端成功、缺失客户端证书和不可信客户端证书拒绝。C/mTLS 互操作成功
路径只授予一帧下行额度，并靠逐帧 `WINDOW_UPDATE` 接收六帧回复，覆盖旧的一次性窗口
在短回答中被耗尽的问题。
MiMo 测试另外启动本地 HTTPS 夹具，使用非私人固定数据验证 ASR/对话/TTS 请求、SSE 分片、
格式转换、首帧早于 EOF、背压、取消后下一轮、超时、401、重定向与截断失败；长期记忆测试
覆盖播放确认后提交、重连恢复、跨设备隔离、清除、撤销、保留上限、私有文件和数据库故障。
测试不访问外部模型；重采样用例需要 PATH 中有 FFmpeg。
成功标记为 `SHANIU_GATEWAY_TEST_PASS`。

HELLO payload 可为空（旧固件，无控制能力），或为 4 字节大端能力位图。
bit 0 为音量控制，bit 1 为播放完成确认，bit 2 为设备状态上报；能力位不充当设备身份。
音量消息均使用 turn_id=0：17/VOLUME_GET 无 payload，18/VOLUME_SET 为大端 uint32
百分比（0–100），19/VOLUME_REPORT 为大端 uint32 请求序号、int32 结果、uint32 读回百分比。
请求序号不能为 0 或 UINT32_MAX；成功结果为 0、百分比为 0–100；失败结果为负 errno、
百分比必须为 UINT32_MAX，表示未知。设置请求的值不能直接当作成功读回值。
板端通过产品回调接入媒体策略接口，仅有回调时声明音量能力；当前音频后端不依赖
CONFIG_BK7258_PREFERENCES 即可提供运行期音量。设置仅改变运行期音量并跨语音轮次
保留；启用 preferences 时持久化默认值仍由独立偏好路径管理。读回是媒体策略档位，
非独立 DAC 寄存器或声学测量。console 已接入该回调协议，服务端只接受当前未完成请求的回报，
超时或取消后的迟到回报不更新快照；重新查询才能恢复已知状态。

20/STATUS_REPORT v2 是设备到 Gateway 的 52 字节固定大端帧，turn_id=0。前 20 字节
上报电量百分比、充电/电池状态、mV 与固件四元组，随后 32 字节是当前 MCUboot
canonical DER 公钥根的 SHA-256；每组未知值均有明确哨兵，版本字段必须同时已知或
同时未知。当前 AIDK 只发布 health worker 实测的充电状态与 mV，不从电压猜测百分比，
也不在语音 owner 中同步读取电池设备。Gateway 对外仍只投影已有 console 字段，
原始 mV 和公钥根保留在连接状态中供兼容性判断和受控诊断使用。

协议对应实现是 `app/bk7258/bk7258_voice_companion.[ch]`。两端改动协议常量或状态规则时，
必须同步更新互操作测试；本目录的测试通过不等于 AIDK 实板闭环通过。
