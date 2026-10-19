# 2026-10-19 ACL 缓冲区生命周期修复实测

本记录接续 MTU 超时定位，记录用户按下 RESET/CEN 后的实际恢复、
771 故障复现及 772 修复验证。
此前记录保留为当时的证据快照。本轮连接路径通过；不代表语音、资源安装或整机验收。

## 771 复现与源码证据

物理复位后，COM9 回读 AP READY、RPTUN CONNECTED、故障计数 0，
770 已确认运行于 B 槽。COM16 使用原有受保护工作台凭据完成版本读取。
按现有签名和目标身份核验后，仅向非活动 A 槽写入 771 的 CP/AP 配对映像。
Loader 成功，启动日志在 11.846 s 记录 `BOTA TRIAL CONFIRMED slot=0 counter=771`。
该次采集工具误用 `BKOTA` 作为预期正则而报告 error；原始启动日志明确确认成功，
不能把工具断言失败写成固件启动失败。772 已使用正确的 `BOTA` 正则。

原绑定手机、原 App 保持不变。22:17:52 发起连接，服务发现成功，
22:17:56 收到 MTU 71/status 0，随后在订阅阶段超过 30 s 超时。
板端回读 `AP_EXCEPTION`：CFSR `00008200`，BFAR `280a0000`，
PC `021d9248`，心跳 328，故障计数 1。CP 控制台仍可应答。

经 771 manifest 的 ELF 哈希核对后，反汇编将 PC 定位到 `hci_rx_work`
中内联的 `hci_acl`：从 `buf->data=2809fffe` 读取 ACL 长度时，
`ldrh [r3,#2]` 越过了有效边界。这是本轮实际故障现场，
不是通过主机模拟推定的板端异常。

固定 NuttX `5ecdd18b230acd2a86068cb4559b9f3f59363857` 的所有权关系为：

- `wireless/bluetooth/bt_hcicore.c` 的 worker 在 `hci_acl()` 返回后释放输入缓冲区。
- `bt_conn_receive()` 将同一缓冲区交给重组或 L2CAP；完整包和错误路径会消费引用，
  首碎片则保留引用至后续处理。原调用链只有一个引用。
- 消费者可能提前释放并复用对象，worker 随后再次释放，破坏新入队对象或首碎片。

这是确定的所有权缺陷，与本次访存异常一致。之前 770 的 `PRIMARY_TIMEOUT`
没有相同故障现场，不能据此断言其全部原因已得到证明。

## 最小改动与主机验证

在已有芯片兼容入口 `chips/bk7258/ap/bk7258_bt_hci.c` 的
`__wrap_bt_conn_receive()` 中，在调用消费者前增加 `bt_buf_addref(buf)`。
消费者与 worker 各持有一个引用；原连接引用释放保持不变。
改动仍受 `CONFIG_BK7258_BT_CONN_RX_REF_COMPAT` 和既有上游源码哈希检查约束。
Kconfig 说明同步更新；本轮没有修改官方 NuttX 源码。

新增 `tests/host/bk7258/test_hci_acl_lifetime.py`，提取实际 worker、ACL 入口、
连接重组函数与 wrapper 编译运行，覆盖完整包、同一池对象提前复用、首碎片保活、
续片完成、无首片续片错误和未知 handle。保存的旧 wrapper 实际失败
（`ownership_error=5`）；修复代码通过。另 7 项 HCI 同步生命周期和内核兼容检查通过。
这是确定性宿主所有权回归，不替代 NuttX 并发调度或实板验证。

Black 24.10.0 和 `git diff --check` 通过。固定 NuttX nxstyle 在旧、新 C 文件上
产生相同的 8 条既有诊断，未新增诊断；不记作 nxstyle 全量通过。

## 772 部署与实际结果

复用 771 的隔离构建树，仅同步本轮 HCI 源文件与 Kconfig。增量构建成功，
CP、BL1、BL2 ELF 哈希不变，只有 AP ELF 改变；角色配置、分区和公开信任指纹不变。
沿用批准的 development identity，签名包 `shaniu-acl-rx-lifetime-772`，
版本 `0.7.126+772`。package/trust 检查和正式提取均通过。
包 SHA-256：

`86a45d6f78bb2310b773a3594ee3b53f32cf431b84dca05c8a2795c424053ac1`

写入前再次回读 771 为已确认 A 槽。772 仅写入非活动 B 槽：
CP `0x352000+0x132000`、AP `0x484000+0x20f000`。
没有写入 boot、持久数据或校准分区。单次 Loader 运行成功，
启动后 12.201 s 自动确认 B 槽和安全计数 772，180 s 启动采集通过。

| 观察 | 实测结果 |
| --- | --- |
| 手机与 App | 原 Mi 10、原绑定，App code 56 / `0.7.26-shaniu-ota-retry`；未重装或清数据 |
| BLE 握手 | 22:29:03 连接，服务发现成功；MTU 请求 185、返回 71/status 0；随后完成管理连接 |
| 手机 UI | 显示“管理已连接”，设备确认音量 53%；更新页读回 `0.7.126 · 772` |
| 生命周期 | 访问全部底部页签，返回设备页；进入系统桌面再返回，管理连接保持 |
| 观察窗口 | 22:29:03–22:31:59，App 进程只有一次 GATT connect；0 次握手超时，无断开回调 |
| PC 工作台 | 原身份绑定凭据完成 COM16 鉴权；info 和 status 均退出 0，读回 772、音量 53、runtime_error 0 |
| 板端健康 | AP READY、RPTUN CONNECTED、supervisor HEALTHY；主/副心跳 1747/1752，故障/恢复 0/0 |
| 槽位与身份 | B 槽 `pair=confirmed version=0.7.126+772`；设备身份摘要与写入前一致 |

本轮没有改动 App 数据、绑定、用户设置或默认资源。启动渲染日志仍为
`shaniu-cyan-v3 revision=3`。没有将界面文本或渲染日志等同于用户目视、听感验收。

剩余边界：状态仍为 `ready=false`，App 提示“语音服务尚未就绪”；本轮未处理该问题。
本轮未执行 resources/scenes 授权、资源安装/试用、NFC 实卡或语音物理交互验收，
也没有进行长时间压力测试。连接修复的结论限于上述真实设备和观察窗口。

原始证据保存在工作区 `out/shaniu-mtu-repair-20261019/`。
`result-772.json` 汇总本轮结果，`SHA256SUMS` 固定文件哈希；
771 的故障日志、旧 wrapper 和旧结果文件均保留。

## 提交前审查与范围

提交范围为 ACL 引用交接、Kconfig 说明、对应宿主回归及本记录。
回归已接入现有 `run-provision`，由已有 CI 在固定依赖工作区执行；
缺少 NuttX 源码时明确失败，不以 skip 计作通过。
AP 心跳观察探针和其他既有开发改动保留在本地，不包含在这次修复提交中。

772 来自记录中的隔离开发工作树，包含此前未提交的稳定性和诊断改动；
本提交不是该固件的完整源码快照。提交基线固定的 NuttX
`76354c637858ecb0aa4601629327acb6f44a26bb` 与 772 工作区的
`hci_acl`、`hci_rx_work`、`bt_conn_receive`、`bt_conn_reset_rx_state`
函数体逐字一致，因此引用修复也适用于提交基线。
8 项相关主机回归、层次检查和格式检查通过；实板结论仍限于上表的 772 工作树。
