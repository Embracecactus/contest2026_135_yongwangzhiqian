# 2026-10-19 电脑资源流程软件验证

按已评审的 N3 计划执行。Windows 当前串口及 PnP Ports 枚举均为空，ADB
也没有连接设备；未沿用历史 COM9/COM16 作为当前连接事实。用户随后明确
“先完成软件部分，实板验证待连接后执行”。OTA 与升级重连专项继续放在最后。

## 独立测试包

- 内部 ID：`n3-resource-hil-20261019a`，预期安装文件名
  `n3-resource-hil-20261019a.bkep`。新的内部 ID 避免覆盖既有素材；接板后
  仍须读取当前目录确认不存在同名包，不能用历史目录替代。
- 108634 字节，18 个条目，SHA-256：
  `df0a7af8672a71c815b21c68a3bc03826efa9a32e485f8d326504cf2ce91a48b`。
- 来源为仓库原创 `shaniu-cyan-v2.json` 与 PNG，仅派生修改包 ID 和描述。
  原素材和默认包未修改。保留派生 JSON、atlas 副本、CLI 命令和校验输出；
  使用保留输入重新打包，哈希一致。
- 公开 CLI `verify eye-pack` 已验证格式、长度、内部 ID、TOC/载荷 CRC32。

## 软件增量与验证

原 `test_lost_ack_resume` 在同一个客户端上继续操作，不能证明新会话恢复。
本次扩展这个既有测试 ID；不增加公共命令或修改固件/产品实现。

原生测试 peer 增加仅供主机夹具使用的会话重开标记，保留 worker、文件、
epoch 与作业，同时关闭旧 SDC1 会话并新建未认证会话。TLS 边界在原生端
确实受理请求后丢弃响应、返回 EOF。随后每个操作新建 TLS 连接并重新认证。
该标记不是产品协议，不能代表物理 USB 拔插、授权撤销或产品会话失效。

扩展后的五个分支通过：

1. BEGIN 回执丢失后查询，再从确认偏移 0 续传；不重复 BEGIN。
2. 第一个 APPEND 回执丢失后查询，从确认偏移 4096 续传。
3. FINISH 回执丢失后查询/等待完成，不再发送资源写入命令。
4. 新会话显式取消并回读 CANCELED、资源已释放；随后拒绝 resume。
5. 等待设备原 TTL 到期，回读 CANCELED/-ETIMEDOUT、资源已释放；拒绝 resume。

检查了每次连接的 AUTH/序号 0、相同作业编号、回执文件字节不变、续传无
BEGIN/TTL 参数、剩余时间递减。初次五分支因旧夹具不能重开会话而失败；
补齐夹具后通过。这是测试覆盖缺口，不是发现了产品断线缺陷。

- 当前源码增量构建 `build/test_display_job_control` 通过。
- 使用本次独立包执行 `ResourceFlow` 全部 13 项通过，涵盖上传、取消、
  丢回执、新会话恢复、旧回执拒绝、文件变化及输入校验。
- 同一独立包经正式 HTTP → Python TLS 客户端 → 原生会话/作业/安装器
  的现有网页上传测试通过。这里的安装结果仅属于主机临时文件系统。
- Black 和相关文件 `git diff --check` 通过。独立只读审查未发现确定缺陷。

复现测试包流程时，从仓库根目录运行：

```sh
make -C tests/host/bk7258 build/test_display_job_control
python3 - <<'PY'
import sys, unittest
from pathlib import Path
sys.path.insert(0, 'tests/host/bk7258')
import test_workbench_resource_flow as flow
import test_workbench_web as web
flow.PACK = Path('../out/shaniu-resources-20261019/package/n3-resource-hil-20261019a.bkep').resolve()
suite = unittest.defaultTestLoader.loadTestsFromTestCase(flow.ResourceFlow)
suite.addTest(web.WebTest('test_http_upload_reaches_tls_and_native_installer'))
raise SystemExit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
PY
```

## 接板后继续的位置

先重新核对端口/设备身份、固件、当前默认包与 revision、授权范围。历史电脑
授权证据仅有 tasks/camera；resources/scenes 须经正常 App 流程取得并回读。
重新授权会替换原授权，应保留所需 tasks/camera 权限及原受保护配置文件。

随后执行：目录冲突检查 → 安装并确认 DONE → 目录回读 → 限时试用及恢复
原画面 → 取消/中断分支 → 试用、任务、相机及退出的组合窗口。中断后先查询
原回执，仅同设备/文件/epoch/nonce 且原 TTL 有效的 receiving 作业可续传；
提交中只查询，完成只确认，其它失效或未知状态不得自动重传。不持久修改默认包。

本轮未连接物理板或手机、未授予权限、未写设备资源/默认选择、未烧录/升级、
未提交或推送。实板安装、USB 拔插恢复、实际 LCD 画面、组合资源释放和 NFC
实体卡仍待相应条件。主机测试不替代这些验收。

工作区证据：`out/shaniu-resources-20261019/` 中的 `connection-preflight.json`、
`package/`、`baseline/`、`native-peer-build.log`、`reconnect-red.log`、
`reconnect-green.log`、`resource-flow.log`、`http-resource-flow.log`、
`source-sha256.json`。初次绿色日志记录扩展基本路径，后续完整 13 项日志对应
增加 TTL/无重复写入断言后的最终测试源码。
