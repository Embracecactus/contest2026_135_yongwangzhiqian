# 决赛集成证据说明（2026-10-09）

本说明汇总截至来源提交 `51115602dcfea419cd9996357bb47efdb862e2bf` 的可复核证据，不新增功能结论。Agent 依赖固定为 `20890a97b9515cce3de34006ad7a9b1746a109ed`。

## 来源快照与候选边界

- 官方比较基线：`6052156862784037a449b9776dfc5c66b36518f6`。
- 基线集成提交 `558ba636ace511fc696a742c7c2a654bd73a93d9` 的 tree 与来源树相同：`ec7b7846cdd7da973e00369a64bc1eb36fd04029`。
- 基线快照从上述官方基线创建，不携带中间开发提交。
- 本说明不声明尚未创建的最终候选提交，也不预设其 CI、PR 或检查结果。

## 本次入口测试

修改入口前冻结来源测试：团队项目解析到个人 fork，且缺少精确事件来源校验，
得到 1 个断言失败及 18 个入口缺失错误。最终受影响测试为来源/交付/权限 12 项通过，
既有 runner gate 30 项通过。最终 PR 合并候选的完整门禁以其 Actions 运行结果为准。
只增加来源解析与现有工作流接线，未重做采集、播放或 Agent 修复。

来源测试 Red 日志 SHA256：`b43d294a2414068197182943f206e27dca746401ff9446dab6dd9320b13a051c`；
Green 日志 SHA256：`50121da6f7b08041c28fa6ab66085b33187956675b92430f5ca0d11ba46a0be3`。
完整新候选日志和产物由对应 Actions 运行提供，不将本地路径当公开下载地址。

## 首次 PR 候选与同步修复

[PR #125](https://github.com/open-vela/contest2026_135_yongwangzhiqian/pull/125)
首次候选 `40593e6704fe9c117dd1b8b5e19400bb218710e4` 的父提交为官方 base
`6052156862784037a449b9776dfc5c66b36518f6` 和 head
`b077a95a4483382dfb42d597e24c1e17fc7491bd`。
[自动运行 37903352486](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/37903352486)
通过事件身份/来源测试后，在 `repo init` 失败：默认抓取 heads 无法取得仅存在于
PR merge ref 的 SHA，未进入构建。未重跑同一候选。

修复明确传入 manifest upstream ref，并为团队项目保留同一 upstream；revision
仍为不可变 SHA。来源测试针对该缺口先 Red 后 Green（12 项通过）。在独立临时
workspace 用真实远端验证后，manifest 和团队源码均为上述候选 SHA。

第二次自动运行 [37904076270](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/37904076270)
在事件解析失败：webhook `merge_commit_sha` 与 Actions `GITHUB_SHA` 不一致。
按 [GitHub 事件定义](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request)，
以 Actions 的候选 SHA、实际检出和 base/head 父提交为准，webhook merge 字段仅记录。
修复将真实检出校验提前到同步前，未取消 SHA/父提交门禁；测试先 Red 后 Green，
并在实际候选 checkout 上复现了不同 merge 元数据的通过条件。旧失败运行保留。

第三次 [37904410901](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/37904410901)
已通过事件/实际父提交验证，但 runner 的发行版 `repo` 启动器不接受
`--manifest-upstream-branch`。现使用通用的 `-b "$GITHUB_REF"` 初始化，随后在
任何依赖同步前断言 manifest HEAD 等于 `GITHUB_SHA`；ref 已移动则 fail closed。
团队项目仍固定 SHA 并声明 upstream。没有放宽校验或升级工具/依赖来绕过错误。
来源测试先 Red 后 Green，真实 PR ref 初始化后再次核对了候选 SHA。

本机预检完整门禁收集 743 项：695 PASS、7 FAIL_ASSERTION、41 SETUP_ERROR，
225 项完整性错误，**未通过**。本机沙箱限制 socket 与 Gradle 缓存写入；在允许
本地 socket 的同一环境复验，AUD-03 cancel-blocked-next 及三个 LIFE-02 采集
用例通过。其余不改记 PASS；最终候选仍由自动 CI 完整执行构建、Android、合同
与独立交付校验。

## 本次交付文件完整性

既有 CI 的 build manifest 记录 ELF/配置哈希，但工件没有其原件。现有工作流
定向增加 17 项：BL1/BL2/CP/AP 的 ELF 和 map、4 个原始 BIN、CP/AP seed
与 resolved 配置、分区 CSV。复制前检查构建清单哈希与路径边界；独立 job
重新检查哈希、文件完整性和标准配置的工程开关。未新增 QEMU 接入或板端启动改动。
同一来源测试组补充真实文件夹具，先因缺导出函数取得 Red，修复后 12 项通过；
涵盖错哈希、越界路径、缺 map、与哈希一致但误开的工程配置。不增加同类测试组。
最终候选的可下载工件和结果仍以 PR 对应 CI 为准，本文不把本地文件充当公开附件。

[运行 37906784140](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/37906784140)
的候选 `c68e39139e27f926efecd29cf2e2067c3c34b6f2` 完成固件、APK、
888/888 合同、70 个 host 构建与两个变异检查，并导出上述 17 项；运行整体失败，
原因是独立校验器要求 manifest 显式含 `path`，而 Repo 在 path 与 name 相同
时合法省略该属性。先在现有来源测试组复现 Red，再按 Repo 缺省规则修复，
保留唯一项目、SHA 和 remote URL 严格检查。来源 12 项、runner 30 项通过；
下载该候选原始工件后，哈希、源码、标准配置、包签名及 release/build 一致性
在修正后的校验器下通过。该本地复验不改变原 CI 失败结论；修复提交的新候选
仍须自动完成全部门禁。

## 已有 CI 证据（复用，不是新候选评分）

来源提交的 [GitHub Actions run 37890100009](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/actions/runs/37890100009)（attempt 1）已完成并成功：`build` 与 `verify-delivery` 两个 job 均通过。

- 收集 888 项：PASS 888，断言失败、setup error、not run 均为 0；70 个 host 构建通过。
- 两个既有变异检查被检出：MSC 提前释放、Wi-Fi 更新误清云配置。
- 该 run 的收集结果 SHA256：`411230759e620854916eee82f7622b128ba3cca329293a98b515510f1bfeca41`。
- 这是固定来源提交的既有 CI 证据，不能作为最终候选的新 CI 分数，也不等同实板验收。
- 平台 CI 工件按 7 天保留；长期复核依赖提交、此 run 链接和下列可再生成的输入，而不是把临时工件当永久发布物。

## 模块与验证边界

| 模块 | 已实现/检查 | 主机或 CI | 工程 HIL | 独立待验 |
| --- | --- | --- | --- | --- |
| 工厂构建、首次使用与配置保持 | 正式开发签名/工厂物化入口；Wi-Fi 与云设置独立保存 | 冷构建、配置保存/重载/请求构造检查 | 本轮复用 701 保持性记录，不重做首次使用 | 新板首次认领及多介质恢复 |
| K2、存储与资源恢复 | 原协调器、资源安装与恢复路径 | 主机故障/取消/恢复合同 | 沿用各版本已记录的覆盖，不扩大 701 结论 | GPIO 去抖、实体键、深睡及真实 App 安装 |
| 采集 stop/cancel 所有权 | producer join、abort/close 所有权与 STOPPING 错误分类 | 生命周期、读/关闭竞争检查通过 | 两次采集退出、自动交接/取消/下一回合证据复用 | 停滞 Media 服务的总取消上界 |
| 受控语音流水 | 实际请求/解析、Agent、分句 TTS 队列、解码/重采样、Media 路径 | 固定输入和受控内存 HTTP 响应回归 | 701 正常→取消→下一正常会话证据复用 | 真实云延迟、声学首声、音质 |
| 本地唤醒 | 模型、前端、阈值和本地监听保持 | 配置保留/请求构造覆盖 | 701 复用记录含监听与 KWS 统计 | 首次启动延迟状态迁移、实体唤醒体验 |
| 标准产品隔离 | `openvela_ap` 关闭三项 audio validation；无 fixture/固定响应入口 | 当前源码的标准 CP/AP 构建及 ELF/BIN/符号检查 | 未刷写标准二进制 | 标准产品实板部署与回归 |
| 手机、绑定、OTA | Android 与协议代码保留 | 现有测试覆盖 | 本轮未连接实体手机 | 真实配网、绑定、BLE、App OTA |

取消路径的已知限制必须保留：一次空读 poll 为 100 ms；这不是 read/abort 总上界。受控测试中的 2 秒条件等待只验证关闭顺序。对失去响应的 Media 服务，未证明有限取消 SLA。

## 701 工程 HIL 与标准构建

701 是已封存的工程 `openvela_ap_audio_validation` 镜像的历史签名下载、启动和运行读回证据。其构建清单、1291 个归档文件和输入清单已与本来源/Agent 复核匹配；本轮没有重新烧录或读取实板状态。

工程流水的外部 ASR、LLM 与 TTS 是内存受控响应；设备仍执行请求序列化/解析、Agent、TTS 队列、Media 与清理路径。它没有真实云请求、DNS、网络 TLS 握手或小米套餐性能结论；首 Media 写入也不是声学首声。

标准 `aidk_ai_toy / openvela_ap` 已在相同来源构建，但**未刷写**。其 resolved 配置中 `BK7258_AUDIO_PLAYBACK_VALIDATION`、`BK7258_AUDIO_CAPTURE_VALIDATION`、`BK7258_AUDIO_PIPELINE_VALIDATION` 均关闭；ELF/BIN 未含 fixture、固定 endpoint、假 key、测试响应或 BKPIPE 入口。该构建证明产品二进制隔离，不替代工程 701 HIL。实际 CP/AP 配置也未启用
`BK7258_ENGINEERING_TEST`、`BK7258_FACTORY_DIAGNOSTICS` 或
`BK7258_POWER_PREPARE_VALIDATION`，不是只读取 Kconfig 的默认值。

## 输入与产物身份

| 输入或产物 | 固定身份 |
| --- | --- |
| Beken SDK | `cb080de1655d579c7593ecf504c440997c4c137b` |
| NuttX | `76354c637858ecb0aa4601629327acb6f44a26bb`，overlay 447 文件 |
| apps | `550cd3ba60a03f8ebf9ac7b72f6eed6aea3bedbe`，overlay 32 文件 |
| Agent | `20890a97b9515cce3de34006ad7a9b1746a109ed` |
| team 输入 | 649 文件，来源 `51115602dcfea419cd9996357bb47efdb862e2bf` |
| 标准 AP config / ELF / raw BIN | `8517b8cf51d893ccbe0a6efc1441a342affcee920ba515c00187e24c5d091ec1` / `3e104538800d4ee7d78024651c0b5cd858a462f6266bc1ddc067de3e173d4cc0` / `734f60202179464bba0ff2309b21a779bf8fef565f7458f274cd84847cf0f652` |
| 标准 CP config / ELF / raw BIN | `dd503376e1b5436c9221274dc5a9ff86bf2e49660dfad4d306292e1113d82bed` / `7d2d043d9f99689780632cd60ef56a69e8d00a3277049b49c29d8f2bb8ccde43` / `cd9031534fa564d2a3d4824659a036d3667574072e2a78f546b9e01445d3d701` |
| 701 工程 AP config / ELF / raw BIN | `2c0e7838…47bf1` / `64bfbdf5…2ef9` / `b1091e5c…b177` |
| 701 工程包 | `21e80a79da04ecdfed0cdccb00bb9fae331b659505ed26cbba9d85aa10b412a1` |

701 工程产物的省略号表示完整 SHA256 由匹配 build manifest 和封存交付清单保存。分区、加载地址、完整 ELF/map、resolved 配置和输入清单属于本地 HIL/发布证据摘要；不在本文公开原始设备日志。

## 未关闭项目

真实且获授权的云服务、实体手机流程、物理按键、声学与功耗测量、可独立恢复的深睡，以及历史 HardFault 对应故障序列仍需单独验收。首帧瞬态、Media mix/EOF 诊断与队列 rebuffer 也未因受控数字路径通过而关闭。
