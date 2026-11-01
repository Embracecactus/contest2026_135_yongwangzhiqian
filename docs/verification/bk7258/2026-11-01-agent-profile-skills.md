# 2026-11-01：情绪、事实记忆与 Runtime Skill 软件验证

本次实现覆盖 BK7258 产品代码、Android 控制入口和 Skill 签包 CLI。
验收层级为主机行为检查及 AIDK 增量构建；**实板样本数为 0**。
本轮没有烧录、安装 App、录音或实机断电重启，不把以下结果当作比赛实测数据。

| 功能 | 已实现行为 | 当前边界 |
|---|---|---|
| 情绪与播报 | `reply_expression` 接收五种情绪；同一请求的 Media 输出开始后申请临时表情，结束或取消后撤销；旧请求不能修改新一轮 | 同步点是软件输出事件；未测扬声器首声与屏幕帧差，未调整 TTS 音色/语速 |
| 事实记忆 | 明确前缀提取称呼、偏好、近期话题；成功播报后保存，取消轮次不保存；App 查看、纠正、删除；与历史共用加密快照 | 需开启记忆；每类一条、每条最多 96 UTF-8 字节；不是任意对话摘要 |
| 事实一致性 | 纠正或删除同时清理近期历史；版本冲突拒绝；存储结果不确定时阻止继续读取旧缓存；重新绑定后恢复已发布快照 | 已验证主机存储重载，未验证真实设备跨天与断电保持 |
| Runtime Skill | 专注助手、设备体检、识物解说三种场景；本轮权限固定，执行入口检查工具和参数；只允许读当前 Skill 说明 | 使用现有产品工具与云对话；未测三个场景的实机完成率 |
| 外部 Skill | App 独立导入发布者公钥及签名包；P-256/SHA-256 验证原始清单；权限只能缩小；安装后默认停用；重启重新验签 | 最多一个外部包；只允许现有三种 profile；未配置产品发布者或安装实际签名包 |

App 入口为“隐私与记忆 → 查看与编辑事实”和“专注与陪伴 → 运行时技能”。
修改通过已有认证设备会话执行，界面等设备回读确认后才显示成功；断连不会自动重放。
更新发布者信任会清除原外部 Skill，界面在操作前说明此影响。

三个内置触发示例：

- `专注助手：开始十分钟专注`：按请求操作专注计时器，再检查状态。
- `设备体检：检查状态`：查询设备状态和加速度；不可调用相机。
- `识物解说：看看这个`：调用现有相机识物；失败时不能虚构识物结果。

| 检查 | 结果与证据范围 |
|---|---|
| `run-agent-profile` | 通过。情绪提前设置、开始输出、结束/取消、旧请求、清理重试；真实 `message_bus` 的历史回调与完成事件；真实 SDC1 会话的认证、分块写入、应用和回读。ASR/Media/显示边界为受控主机输入 |
| `run-memory-facts` | 通过。编码、旧快照兼容、实际 AES-GCM/事务存储、纠正/删除及重载、写入失败与结果不确定处理；额外验证 JSON 生成过程的 17 个分配失败位置无遗留分配 |
| `run-runtime-skills` | 通过。三种权限集合、非法参数、禁用、跨 Skill 文件访问、重复/过期请求、无信任根与签名篡改拒绝；导入/启用持久化后通过 `fork`/`exec` 新进程恢复 |
| `run-runtime-skills-store` | 通过。真实事务存储的 CAS、失败回滚及发布后目录 `fsync` 失败；重新载入可区分已发布的新版本 |
| `test_runtime_skill_package.py` | 3 项通过。真实 OpenSSL 签名、不可覆盖输出、权限校验；另用设备同源 C 验签器检查 6 组合法/不合法签名清单，含权限不符、尾随数据、未知/重复字段与非法 UTF-8 |
| `test_shaniu_response_length_route.py` | 通过。实际 HTTP 适配器调用上下文注入；原响应长度路径回归通过 |
| 本地意图与表情原路径 | `run-agent-local-intent`、`run-expression-trial`、`run-expression-ownership` 通过；防止本次接入破坏既有路由和显示所有权 |
| Android | `:app:assembleDebug :app:testDebugUnitTest` 通过。325 项中 316 通过、9 跳过，0 失败；新增 `AgentDataControllerTest` 的 9 项全部通过 |
| 固件 | `build --board aidk_ai_toy --boot direct --jobs 8` 通过，使用既有构建树增量编译；最终日志未出现编译 warning/error |
| 格式 | 8 个新增 C/H 模块的 pinned `nxstyle` 通过；3 个本次 Python 模块/测试的 Black 24.10.0 检查通过；`git diff --check` 通过 |

没有运行全库发布验收或其他板卡 clean build。CLI 主文件的 Black 全文件检查仍有
两处本轮之前的格式差异，本轮未重排这些无关代码；环境没有 ktlint/shellcheck/cmake-format。

复现本次主要检查，在团队仓库运行：

```sh
make -C tests/host/bk7258 run-agent-profile run-memory-facts \
  run-runtime-skills run-runtime-skills-store run-agent-local-intent \
  run-expression-trial run-expression-ownership
python3 tests/host/bk7258/test_runtime_skill_package.py
python3 tests/host/bk7258/test_shaniu_response_length_route.py
python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct --jobs 8
```

Android 命令在 `android/shaniu-companion/` 运行：

```sh
./gradlew :app:assembleDebug :app:testDebugUnitTest --console=plain
```

签包命令、清单样例与导入流程见仓库 `tools/bk7258/README.md` 的
“Runtime Skill packages”。主机签名测试使用固定 mbedTLS 依赖公开的测试密钥，
不是产品身份；没有生成或更换设备的启动/OTA 信任根。

构建身份：团队 HEAD `930d75ae34f215dcaf8707751d5e27cc59ec7ca9`，工作树有既存
未提交修改及本轮修改；718 个构建输入的摘要为
`7eeddfba8f4ed547efcf693e84951fc212e526c89aabe002e8869971c2d4c33d`。
目标为 `aidk_ai_toy / direct`，layout 为 `bk7258-510173147382a879`，
AP 为 `bk7258-role-975d3ebb7cedcee4`，CP 为 `bk7258-role-cc4a86821ce9fdda`。

| 构建产物 | 字节数 | SHA-256 |
|---|---:|---|
| AP direct image | 1999676 | `f8bb9a72e74e7cb57c70202ce04ea572f574debb319dbe4a054a5a89e130153a` |
| CP direct image | 1160420 | `649298ec818e6316d918ebb77c79a12cf305e93a28891b91e68c9121505603d6` |
| Android debug APK | 18885715 | `1d8f0983ee39656854316a85758eafc32ac683fe84debf7a195d9557d0b19ba5` |

固件仅为本次 direct 构建输出，不是可直接全片烧录的完整交付包，也不是签名 OTA。
manifest 位于工作区
`out/bk7258/aidk_ai_toy/app__openvela_ap/bk7258-510173147382a879/releases/direct/build-manifest.json`。
日志、manifest 副本、APK、JUnit 记录及本轮源码哈希保存在工作区
`out/agent-profile-skills-20261101/`；`evidence.json` 记录逐项身份。

检查器来自 NuttX `892be7ba7dc468f11f32eade4c553cc76b0219c3`；apps 为
`550cd3ba60a03f8ebf9ac7b72f6eed6aea3bedbe`；Agent 为
`35df4b347b17f07c972eb6e7e14a14fee1be78e4`。Agent 依赖带有本轮之前的修改，
因此不宣称官方依赖工作树干净；本轮没有修改其源码。

主机编译器的静态栈报告中，Skill App 应用入口栈帧为 2784 字节；较大的记录和
快照缓冲放在堆上。该数字不是板端峰值，不包含嵌套调用、任务栈余量或堆峰值。
真实重启保持、屏幕/声音同步、三场景完成率及运行内存仍需在该候选固件上测量。
