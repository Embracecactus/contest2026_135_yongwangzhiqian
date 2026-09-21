# 傻妞手机热点可靠性与低延迟对话专项（2026-09-21）

状态：`已修改，仅构建通过；实机验收未执行`

## 1. 基线与工作树事实

- 仓库：`/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian`
- 起始 HEAD：`8e2b0ea53f858860e7cac1215acfbe9c24d356eb`（分支 `docs/final-material-sync-20260920`，
  工作树干净）
- 参考核对快照 `6052156862784037a449b9776dfc5c66b36518f6` 在当前对象库中不存在，
  未能用它做差异核对；本次以工作树实际内容为准。
- 板端当前运行镜像：`641`（本次未重新烧录，串口只做读取采集）。
- App：`versionName 0.5.23-shaniu-rebind`，`versionCode 28`。
- 控制协议 SDC1：`AUTH(1) STATUS(2) CANCEL(3) VOLUME(4) PERSONA(5) CLEAR_HISTORY(6)
  MEMORY_SET(7) MEMORY_DELETE(8) INFO(9) OTA_*(10..14) CONFIG_READ(15)
  CONFIG_BEGIN(16) CONFIG_APPEND(17) CONFIG_APPLY(18) CONFIG_CANCEL(19)`；
  配置事务按 kind 区分：`1` 云端模型、`2` 唤醒模型、`3` 唤醒模型回退、`4` 回答模式、
  `5` 眼睛素材、`6` 唤醒灵敏度。**没有 Wi-Fi kind**，Wi-Fi 只能通过重新配网事务写入。

## 2. 日志事实与计时语义核对

用户提供的两份日志中的现象与下列字段语义已逐个对照实现：

| 字段 | 实际含义（实现位置） | 不能等同于 |
| --- | --- | --- |
| `receive_ms` | `bkcloud_http.c:recv_tls()` 内 `tls->recv()` 的累计耗时 | “运营商网络耗时”：它包含 TLS 解密与等待记录，也包含对端节流后的等待 |
| `slow_reads` | 单次 `recv` ≥ 200 ms 的次数（`bk7258_cloud_http.c:89`） | 丢包/重传证据 |
| `max_read_ms` | 单次 `recv` 最大耗时（打印 `http->max_receive_ms`） | RTT |
| `consume_ms` | SSE 解析 + base64 解码 + 24k→16k 重采样 + **播放写入** 的累计耗时 | “解码耗时”：其中包含阻塞式播放写入 |
| `total_ms` | 整条请求的墙钟时间 | 各分段之和（分段可能重叠） |
| `system_tcp_delta` | 系统级 TCP 统计差值，覆盖所有连接 | 单条连接正常的证明 |
| `stopped=1` | 收到 `finish_reason` 后 SSE 解析器的“已停止收集音频”状态 | 异常终止 |
| `TTS pcm_bytes` | 解码后的源采样率字节数 | 播放时长；`640000 B / (16000 Hz×2 B)` ≈ 20 s，与 68 s 的墙钟不对等，
  说明该轮并非“20 秒音频在 20 秒内到达” |

结论：`receive_ms` 与 `consume_ms` 只能作为“等待对端记录”和“本地消费（含播放背压）”两个
有量纲的分段，不能直接写成网络耗时与解码耗时。`stopped=1` 是正常终态。

## 3. 已实施的修改

### 3.1 TTS 断续：网络接收与播放解耦 + 有界队列 + 首播预缓冲

文件：`app/bk7258/bk7258_agent_cloud.c`

根因（证据）：

- 播放回调原先直接运行在 HTTP `sink()` 调用栈里
  （`bk7258_cloud_http.c:171` → `pcm_output` → Agent 回调 → `audio_playback_write()`），
  一次播放写入会阻塞 TLS 读取；`audio_playback.c:249` 每次写入只有 1000 ms 期限，
  超时直接返回 `-ETIMEDOUT` 并中止整轮 TTS。
- 播放从第一个 PCM 块开始，没有任何预缓冲，而硬件播放缓冲仅
  `period 4×320 帧`（日志 `buffer_frames: 1280`，16 kHz 下约 80 ms），
  任何大于约 80 ms 的到达抖动都会变成可听见的空隙。

修改：

- 在服务适配边界新增**一个有上限、可取消的 PCM 队列**（96 KiB ≈ 3 s@16 kHz mono），
  生产者（TLS 接收线程）只等待队列空间，绝不等待音频设备；消费者线程按播放设备节奏
  调用官方回调，并保持“单次 `is_last` 终态回调”的契约不变。
- 首播预缓冲 8000 B（250 ms），并带 150 ms 启动逃逸：若对端在 150 ms 内无法供满
  预缓冲，就按已到达的数据起播——供给长期慢于实时的情况不是靠加缓冲能解决的。
- 明确区分“暂无数据 / 正在缓冲 / 源流结束 / 队列排空 / 取消 / 网络异常”：
  只在最后一帧交付后置 EOF，短期缺数据不会被当作流结束。
- 新增每轮一行脱敏埋点，把三段分开：
  `AGENT TTS queue ret=… bytes=… total_ms=… first_pcm_ms=… first_frame_ms=…
  max_gap_ms=… gaps=… peak=… producer_wait_ms=… dry_events=… dry_wait_ms=… turn=…`
  其中 `first_pcm_ms` 是首个 PCM 帧被接受的时间，`first_frame_ms` 是首个帧交给官方
  播放回调的时间；两者都不代表已经出声。

### 3.2 重启后不重连热点 / 断网后不恢复

文件：`app/bk7258/bk7258_agent_product.c`

根因（证据）：

- 云端激活只做一次：`pending = ret == -EBUSY;` 之后任何非 `-EBUSY` 失败都把
  `pending` 置回 `false`，而重新置位只由新的产品事件（例如新的 App 连接代）触发。
- 因此“设备先启动、热点后开启”“热点关闭再开启”“启动时挂载尚未完成”都会永久停在
  `BKVOICE configuration ready=0 result=-107/-110`，且不会自行恢复。
- 事务失败时 `bkprov_network_step()` 会回滚到上一份配置，这是正确的；缺的是**下一次尝试**。

修改：

- 新增单一、有界、可取消的恢复调度 `bk7258_cloud_recovery_due()`：
  需要 `g_identity_bound`、不健康状态持续 ≥5 s 才启动，随后按
  5 s→10 s→20 s→40 s→60 s 退避重试，最多连续 8 次计数后仍保持 60 s 上限。
- 重试只清掉**内存中的**服务状态（`product_clear`），已接受的 bundle 与 owner 身份
  一律保留在卷上，由既有 owner 重新装载；不清 `/data`、不删除身份、不清 Wi-Fi/云端配置。
- 错误分类 `bk7258_cloud_retryable()`：连通性类（`-ETIMEDOUT/-ENOTCONN/-EHOSTUNREACH/
  -ENETUNREACH/-EAGAIN/-EIO/-EREMOTEIO/-ENODATA/-ECONNREFUSED/-ECONNRESET`）继续重试；
  授权/格式/能力类停止重试并保留可见错误，等待 App 写入新配置（新 revision 会重置状态）。
- 失败与恢复都只作用于单一 owner，且都在 `voice_channel_is_idle() && !network_busy &&
  !bkprov_owner_busy()` 闸门下执行，不会出现多个重连线程或重叠扫描；本地唤醒与
  已授权 BLE 通道不受影响。

### 3.3 认证后无法修改大模型配置

文件：`app/bk7258/bk7258_agent_product.c`

根因（证据）：云端模型写入路径在解码新记录之后有
`if (!g_cloud_loaded) return g_product_error ? g_product_error : -EAGAIN;`，
随后还用一个**实时 TLS 探测** `bkagent_cloud_verify_service()` 的结果决定成败，
失败即回滚整次写入。于是“离线 / 旧热点失效 / 云端 key 错误 / 无互联网”时，
已认证的 BLE 会话也无法保存新模型配置——正是用户反馈的现象。

修改：

- 去掉“必须已加载云端”的前置门槛：写入只需要已接受的受保护 bundle 与 owner 身份，
  在线状态不再是否决条件；内存中的旧模型只在确实已加载时作为回滚目标。
- 探测失败按类别处理：授权/模型/协议类（不可重试）仍然失败并回滚（不隐藏 401/400/
  协议不兼容）；连通性类则视为“**已保存，尚未验证可用**”，配置保持已应用，
  由 3.2 的恢复调度在网络恢复后再验证。
- 无回滚目标时不使用全零模型做回滚，只保留失败状态。

### 3.4 认证后无法修改 Wi-Fi（重新配网）

文件：`android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/ProvisionActivity.kt`

根因（证据）：App 重新配网时无条件生成新的随机控制密钥
（`ByteArray(32).also { SecureRandom().nextBytes(it) }`），而固件在重新绑定时要求
**控制密钥保持不变**：
`bk7258_provision_network.c` 的 `begin()` 中
`if (!ret && t->revision && (!t->settings.control_key ||
 mbedtls_ct_memcmp(owner_key, t->settings.control_key, 32))) ret = -EACCES;`
——密钥不一致即 `-EACCES`，用户只能通过清除设备数据才能换网。

修改：已在本机绑定的设备复用 `ProvisionBindingStore` 中保存的 owner 控制密钥
（`useControlKey()`），只有首次认领才生成新密钥。密钥既不轮换也不打印。

## 4. 构建与验证证据

| 项目 | 命令 | 结果 |
| --- | --- | --- |
| 固件（AP）增量构建 | `python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct --jobs 8` | `EXIT=0`，`0 error`，`out/shaniu-hotspot-20260921/build/build4.log`；layer gate `PASS sources=506 kconfig=252 legacy-exceptions=2` |
| App | `cd android/shaniu-companion && ./gradlew --offline :app:assembleDebug` | `BUILD SUCCESSFUL`，`app/build/outputs/apk/debug/app-debug.apk`，sha256 `56fc7e42bb8ba57e0ca63c63f3b8ff1d31819ec67348066e5c27e48f44ca361a`（`0.5.23-shaniu-rebind` / 28） |
| 串口只读采集 | `tools/windows-hardware-debug/scripts/debug_session_wsl.sh --console-port COM13 --baud 115200 --action capture` | `DEBUG_SESSION_OK`，`out/shaniu-hotspot-20260921/baseline-capture/serial.raw`（573 B，设备处于待唤醒监听） |

构建产物为 `direct`（无签名诊断链），**不能**作为发布或 OTA 产物；实机验证需要另行授权的
同板基础镜像流程。

## 5. 尚未完成 / 未验收

1. **未执行实机验收**：本轮未烧录、未改动设备状态。TTS 连续性、重启重连、离线改配置、
   Wi-Fi 换网四项都只有源码与构建证据，没有实机通过结论。
2. **首句延迟目标未达标且未测**：实测基线中 LLM 单次非流式请求 3894 ms 已经超过
   p50≤3 s 目标。文本流式需要改动官方 Agent 的 LLM 传输契约（当前为一次性
   request/response，且与工具调用语义耦合），本轮未实现；本次只补齐了分段计时
   （ASR/LLM/TTS 各带 `turn=` 关联号与毫秒耗时）。
3. **CP 侧 `mm_tim_tbtt_compute:4547` 断言无法在本仓库修复**：该符号定义在预编译
   `libwifi.a`（`mm.c.obj`，见 `chips/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9/
   cp-aidk/libs/libwifi.a`），仓库与其依赖 SDK 中都没有对应源码。日志显示断言发生在
   唤醒后（KWS 命中 → 录音 → 打开播放器）且伴随 `tim_tbtt: past_intv 2`，
   属于 MAC 时序/信标间隔状态问题，需要 Beken SDK 侧修复或版本升级（当前 manifest
   固定 v3.1.1.9，不擅自升级）。
4. 官方 Media 播放器在正常结束后会重复打印
   `audio play/resume failed: End of file`（约 6 次后收到 `COMPLETED` 并正常停止，
   `bytes written` 与 TTS 输出字节一致，未发现截断）。该文件属于官方
   `frameworks/multimedia/media/server/media_player.c`，本轮未修改，仅记录证据。

## 6. 本次发现的既有红色宿主机测试（与本次改动无关）

```
python3 -m pytest tests/host/bk7258/test_bk7258_cloud_http.py \
  tests/host/bk7258/test_provision_gatt.py -q
FAILED test_bk7258_cloud_http.py::CloudHttpTest::test_http_adapter
FAILED test_provision_gatt.py::ProductGattTest::test_window_queue_and_connection_generation
2 failed
```

为区分责任，先把本次三个改动文件 `git stash` 后在原始 HEAD 上重跑同一命令，结果同样是
`2 failed`，随后 `git stash pop` 恢复；因此这两个失败是**改动前就存在**的，不是本轮引入。
它们分别覆盖 HTTP 适配层基础 POST 与「窗口队列 + 连接代」语义，正好是本次专项的相邻
区域，建议单独作为后续缺陷处理（本轮不修改测试与这两个实现文件）。

## 7. 唤醒中 CP 断言 `mm_tim_tbtt_compute:4547` 的证据

从 `chips/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9/cp-aidk/libs/libwifi.a` 中抽出
`mm.c.obj` 后可以直接定位（`nm`/`strings`/`objdump`/`readelf`）：

- 编译单元：`/armino_avdk_smp/cp/properties/modules/wifi/ip_ax/macsw/ip/lmac/src/mm/mm.c`
  （`-Os -g -ggdb`，Cortex-M33）。
- 同一目标文件里同时存在字符串 `mm_tbtt_compute: past_intv %d` 与
  `tim_tbtt: past_intv %d`，以及 `mm_sta_tbtt`、`mm_check_beacon`、`beacon_loss_cnt`、
  `lose_bcn_cnt`、`tbtt_move_ongoing`、`MM_SET_BEACON_INT_REQ`、
  `mm_beacon_ies_changes`、`chan_tbtt_updated`、`mm_beacon_is_from_operating_channel`
  等信标时序符号。
- `mm_tim_tbtt_compute` 内唯一一处 `bl <dbg_assert_err>` 的现场指令为
  `movw r2, #4547`（0x11c3），即断言把**行号 4547** 传给断言处理器；
  `.debug_line` 也确认 `mm.c:4547` 正是 0x210–0x224 这段代码，
  紧邻的 `mm.c:4542` 是前置判断、`mm.c:4544` 是一次跟踪打印。

结合 CP 日志顺序
`[cp] [wifid]` → `[cp] tim_tbtt: past_intv 2` → `MAC_DIAGS/PHY_DIAGS DONE` →
`!!!ASSERT at mm_tim_tbtt_compute:4547.`，可以确定机制是：

1. MAC 在重算 STA 下一拍 TBTT 时发现算出来的间隔是**负值**（相对当前 TSF 已经过期，
   日志值为 2），于是先打印 `tim_tbtt: past_intv 2`，再在该行断言。
2. 该函数前后紧邻 `phy_get_channel_switch_dur` 调用与 2300/2288 一类的固定余量常数，
   说明负值来自「剩余时间 − 固定余量 − 信道切换时长」。
3. 因此触发条件是**重算时刻距下一拍 TBTT 太近或已经越过**，常见来源：AP 改变信标间隔
   或换信道（手机热点重开、被挤走后切信道、锁屏节电）、长时间丢拍后重新同步
   （`lose_bcn_cnt` 增长）、以及 STA stop/start 或扫描后带着过期 TSF 回到工作信道。
4. SDK 对「负间隔」没有做饱和或延迟一拍处理，而是直接断言——这是预编译 MAC 的实现
   选择，仓库内无法修改；本次也不再增加 STA stop/start 频率（重试退避最小 5 s、
   上限 60 s，且只在服务确实不可用时触发）。

结论：这不是本项目代码写错的断言，而是手机热点场景下 MAC 信标时序进入负间隔后
被 SDK 视为致命错误。可从本项目侧做的只有**减少不必要的状态切换与负载峰值**，
根治需要 Beken 在 `mm.c` 的 TBTT 重算路径上做饱和/重同步处理。

## 8. 首句延迟：本轮新增的断句流水

`app/bk7258/bk7258_agent_cloud.c` 在上一节队列的基础上新增：

- `tts_parts()`：只按中文 `。！？；`、ASCII `. ! ? ;`（排除小数）与换行断句，
  短于 16 字的片段并入下一段，最多 4 段，全文短于 80 字不拆分；**不丢字、不乱序**。
- `synthesize_parts()`：按句顺序逐条请求，全部 PCM 进入**同一条有上限音频队列**，
  因此句间没有播放器重建的空档；某句失败即停止后续句并保留可见错误。
- 新增 `AGENT TTS parts count=N ret=… bytes=… turn=…` 一行，便于与
  `AGENT TTS queue … first_pcm_ms/first_frame_ms` 对照首句是否真的提前出声。

效果是：长回答的第一段音频不再等待整篇文本的首个音频 token，而是等待**第一句**的；
短回答（<80 字）仍是一条请求，行为不变。代价是最多 4 次 TTS 请求，需在实机上核对比。

## 9. LLM 文本流式：剩余落点（未实现）

官方 Agent 的 LLM 传输仍是 request/response：
`llm_proxy.c:llm_http_call_checked()` 一次性拿到整篇回答，
`agent_main.c:394 voice_channel_speak(msg.content)` 再整篇播报。要做到
「生成第一句就出声」，还需要三处改动（本轮未做）：

1. `packages/ai_agent/include/llm/llm_proxy.h`：新增可选的流式传输入口与
   `llm_set_stream_sink(cb, ctx)`，保留现有一次性入口不动。
2. `packages/ai_agent/src/core/agent_loop.c`：把增量文本按同一断句规则送入一个
   TTS 工作线程；一旦出现 `tool_calls` 立即丢弃未播文本，禁止播报工具 JSON 或推理内容；
   最终回答不再二次整篇播报（避免重复入历史/重复出声）。
3. 本仓库 `bk7258_agent_cloud.c`：新增 `llm_transport_stream()`，请求体加 `"stream":true`，
   用既有 `bkcloud_http_events()` 消费 SSE `choices[0].delta.content` 并增量回调。

这三步都必须在实机验证工具调用、取消与首句出声，因此没有在半成品状态提交。

## 10. 交付产物（2026-09-21）

签名链构建（`--boot mcuboot`，BL1/MCUboot 公钥，rollback floor 642）`EXIT=0`，
随后 `release ota` **PASS**：

- 版本 `18.6.401+642`，安全计数/回滚下限 642，artifact id
  `shaniu_aidk_v18_6_401_642_ota`，MCUboot 公钥指纹
  `4979ece7072284b2582bb9358198fa21baa415d9340d32779ecb0ba9a48dd984`。
- OTA 包 `…-v18.6.401+642-…-ota.bkpack`，SHA-256
  `fb2cb4fd34cfa7ce61b1cc24708e53eff5bce652aaa99e4d5c18a5c813631c3d`。
- `verify package` → `PASS images=2 board=aidk_ai_toy security=signed-ota`；
  `verify trust` → `PASS public MCUboot CP/AP signatures`。
- 负载只写 CP/AP 的 inactive 槽（`target: inactive`，无擦除操作，
  `full_flash_base: not-required`），不触碰 `easyflash`/`easyflash_ap`/`sys_rf`/`sys_net`
  等设备唯一分区。
- **改动确在包内**：构建清单 `inputs.ap.sha256 =
  ea5ffa6b23340b3c6a1fc2cb2ca9b6aba5e55ecb17045f8672f2831619081e50` 与该 AP 原始镜像一致，
  且该镜像内可直接检索到 `AGENT TTS queue`、`AGENT TTS parts`、
  `BKVOICE cloud recovery attempt` 三个新增日志串。

App：`0.5.23-shaniu-rebind`（code 28），SHA-256
`56fc7e42bb8ba57e0ca63c63f3b8ff1d31819ec67348066e5c27e48f44ca361a`。

交付目录（供下载）：`C:\Users\lijian\Downloads\傻妞_热点修复_20260921\`
（`README.md`、`SHA256SUMS.txt`、`firmware/`、`app/`）；同内容的说明文件为
`out/shaniu-hotspot-20260921/README-delivery.md`。

### 全量（BKFIL 8 MiB）镜像未生成的原因

`release full` 强制要求 `--base`（本机完整 8 MiB 读回）与 `--base-evidence`
（`package accept-base` 产出的 `bk7258.accepted-base/1` 证据，绑定板型/布局/设备身份/捕获方式）。
当前工作区没有该材料：637–641 的发布目录已不在 `out/` 下；全机唯一的 8 MiB 捕获是
`logs/bk7258-layout-migration/20260803-221329/pre-migration-flash-8m.bin`（布局迁移之前，
不能作为当前布局的 accepted base）；CLI 不提供 flash 读回命令。
在没有可信同机 base 之前生成“通用首烧”镜像会以清空设备身份/配置为代价，因此未生成。

## 11. 全量（BKFIL 8 MiB）镜像：已生成（按首次全量烧录方式）

按“评委第一次全量烧录”的口径，用工厂整片 `AIDK_AI_Toy_factory_verified_20260815.bin`
（8,388,608 B，`cfbf8d2a3c7f8125d973db2401d84d727ce501e41f9857feaf639c333f967a63`）
作为 accepted base（`package accept-base --capture-method factory-readback
--device-id C8:47:8C:CB:7F:80`，evidence `807dae9b…`），再执行
`release full --version 18.6.401+642 --product shaniu
--artifact-id shaniu_aidk_v18_6_401_642_full`：

- `flash/*-full-full.bin` 8,388,608 B，SHA-256
  `5760771819b42bb2f977b772c1ab510ae8e18b98384ab1a9082503ea538aad46`；
  `package/*-full-full.bkpack` 7,980,186 B，SHA-256
  `6d3ab4e5d249599d8698b6dfcbaf3e24b8e3407546d2d85c8b3adc39f992666f`。
- `verify package` → `PASS images=8 board=aidk_ai_toy security=signed-evidence`；
  `verify trust` → `PASS public BL1/BL2/CP/AP signatures`。
- 写入契约 `writes [0, 8388608)` 整片覆盖；`persistent_data` 0x6E8000(1 MiB) 来自 base 快照。
- 改动进入镜像的证据：`inputs.ap.sha256 = ea5ffa6b…`（含新增日志串的原始 AP 镜像），
  最终容器为转换后镜像，字符串抽检不可靠，以清单哈希链为准。

注意：此前 637–641 链使用的同机 base（`out/shaniu-p0-20260913/private-base/
same-device-current-config-base.bin`，`bdc17699…`，经 `layout621-private-base/
relocated-base.bin` 迁移）已不在本机，因此本镜像采用工厂整片作为 base；
若后续拿到那份同机 base，可在同一构建清单上重跑 `release full` 得到其对应版本。
