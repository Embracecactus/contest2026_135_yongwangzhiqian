# BK7258 T5-Board P0 xTS 与 CP PSRAM 系统堆验证

- 日期/时区：2026-08-27，Asia/Shanghai
- 分支：`feat/bk7258-p0-xts-completion`
- 集成起点：`ecc1c0a185896d6afce165d20ebbf1a270782683`
- 目标板：T5-Board V1.0.2
- 下载与控制台：COM3，BK Loader 2.1.11.15 / UART0 115200 8N1
- Agent layout identity：`bk7258-2b86faab14dca06e`
- 板端证据目录：`out/bk7258/verification/g149-p0-xts/`
- 结论：现役 host 回归和当前代非破坏性 xTS 核心用例通过；破坏性存储、依赖外部
  夹具的驱动用例及受控 fault 仍保持独立门禁，12 小时 soak 经 owner 确认延期。

证据目录属于 owned repo 外的本地构建产物，不进入 PR；本记录保存可提交的文件名、
字节数和 SHA-256 摘要，原始串口/产物在该工作区目录留存。仅检出仓库的复核者可
验证摘要和契约，不能据此声称已重新取得原始串口证据。

复现方法与配置边界见
[P0 调试、xTS 与性能基线适配手册](../../platforms/bk7258/p0-diagnostics-performance.md)。
现役主机夹具的迁移过程和 281 项 cmocka 结果见
[主机回归记录](2026-08-27-bk7258-host-regression-fixture.md)。

## 1. Generation 147～149 的处置

| Generation | 唯一变量与现象 | 处置 |
|---|---|---|
| 147 | 保留上游 16 KiB testsuites runner stack，未给 CP 增加系统堆。冷启动最大连续 Umem 约 21,872 bytes，运行负载后约 15,104 bytes；`cmocka_mm_test` 的 malloc 用例断言失败，后续 worker 断言跨线程进入 cmocka `longjmp` 并触发 HardFault | 仅用于定位，不作为验收镜像 |
| 148 | 临时把 runner stack 缩到 8 KiB。`cmocka_mm_test` 8/8 PASS，但 `cmocka_sched_test` 只有 11/16 PASS；pthread09 需要 10 个并发默认 2 KiB stack，task01～04 也因 `ENOMEM` 失败 | 证明根因是堆容量，不保留缩栈方案 |
| 149 | 恢复上游 16 KiB runner stack，只给 `t5_board_cp_xts` 增加 64 KiB CP 角色本地 PSRAM 系统堆和 `MM_REGIONS=2` | 最终验收镜像；mm 8/8、sched 16/16 和其余 §6 用例 PASS |

generation 147 的 HardFault 不是 PSRAM 或 allocator 元数据损坏：符号化 PC 位于
`longjmp+4`，LR 位于 cmocka runner；触发点是 mm08 worker 在分配失败后使用 cmocka
断言，而断言的 `longjmp` 目标属于另一个线程。generation 148 在缩小 runner stack 后
mm 全过、调度并发因 `ENOMEM` 失败，进一步把问题收敛为 CP xTS profile 的可用堆不足。

最终方案不修改通用 xTS stack，也不靠放宽断言掩盖问题。它把额外内存限定在诊断
profile，并保留生产 CP/AP profile 的原内存预算。

## 2. 实现与配置契约

### 2.1 Chip 能力

- `BK7258_PSRAM_SYSTEM_HEAP` 从 AP-only 能力扩展为 role-local 能力；启用条件仍要求
  PSRAM 和 `MM_REGIONS > 1`。
- AP 可配置范围为 64～512 KiB；CP 可配置范围为 64～124 KiB。公共实现还拒绝
  `size >= BK7258_PSRAM_LOCAL_HEAP_SIZE`，防止覆盖角色私有 PSRAM heap 的边界。
- CP 完成 `bk7258_psram_initialize()` 后才调用 `bk7258_psram_add_system_heap()`；失败
  会进入现有 PSRAM 启动错误传播路径，成功打印
  `BPSR SYSTEM HEAP PASS size=<bytes>`。

### 2.2 T5-Board xTS profile

`boards/bk7258/t5_board/configs/xts/defconfig` 只增加：

```text
CONFIG_BK7258_PSRAM_SYSTEM_HEAP=y
CONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE=0x10000
CONFIG_MM_REGIONS=2
CONFIG_TESTS_TESTSUITES_STACKSIZE=16384
```

因此 CP 的 128 KiB 角色本地 PSRAM 中，64 KiB 继续作为私有 PSRAM heap，另外
64 KiB 注册为 NuttX 第二系统堆区。配置只属于诊断/xTS profile；Agent 产品 profile、
AP 的 640 KiB 角色 heap 和 AP 512 KiB system heap 契约均未改变。

## 3. Host 回归与干净构建

`./tests/bk7258/run_tests.sh` 最终返回 0 并打印 `BK7258_HOST_TEST_PASS`：

| 层 | 结果 |
|---|---|
| 公共模块 | mailbox 31 checks、RPTUN CP 47 checks、RPTUN AP 32 checks、PM activity 与 BL1 policy 全部 PASS |
| BL1 cmocka | 42/42 PASS；policy 目标同时使用 ASan+UBSan |
| BL2 cmocka | 42/42 PASS |
| AP/CP 外设 cmocka | 197/197 PASS |
| cmocka 合计 | 281/281 PASS |

当前 Agent 分区 CSV SHA-256 为
`8c66c2f3d10db658d464580dfce3ea339efc7c2f2c9dc59d4a87ab8013e728cc`。

generation 149 的 MCUboot clean build 同时完成 CP/AP/BL1/BL2。第一次构建环境中的
系统 Python 缺少 `pyelftools`/`cxxfilt`；把已准备的 venv 放到 `PATH` 后同一源码干净
构建通过，这属于主机工具环境问题，不是源码失败。

| 字段 | 值 |
|---|---|
| CP build identity | `bk7258-role-112a6866955649c7` |
| CP build seed SHA-256 | `39d228a62891c6ed3bb8015cd2b7f2a49dbb30bd0ea210e706864cd45b682385` |
| CP resolved config SHA-256 | `bd171569f979d251fd683f5fad41180d969771933697c6f1b415404f5e57c5ce` |
| CP bin | 1,296,652 bytes；`c57207384885cef3cb96dbe830371190c3ec1b61ef601006b3cc9bbd1bba389c` |
| AP build identity | `bk7258-role-c4aeafcd323003ff` |
| AP build seed SHA-256 | `eaa62dcdcd5acb635e205079c595a9d1488bdb49c21613dd7abb59c09a62fabd` |
| AP resolved config SHA-256 | `3d40200a0fecbb540bf9d1b0373b403c74a1b879714db7c659bbf6995cff7ad7` |
| AP bin | 1,045,264 bytes；`3eebe73a886f3a07b195832360afc3ba7c4ba113744604545765950e84490fe3` |
| BL1 bin | 65,376 bytes；`5e88f02f42497ba197e969a84c699fef5bc86e705b457fde06d247861fceaf06` |
| BL2 bin | 13,700 bytes；`058aa85dc768074192ad093b2e5be1644931f4fd0242dfae75a45f61920ab2f4` |

解析后的 CP `.config` 再次确认 system heap=`y`、size=`0x10000`、
`MM_REGIONS=2`、testsuites stack=`16384`。

### 3.1 板测后的通用失败路径复核

generation 149 已下载产物中的 xTS profile 启用了 `BK7258_AP_CONTROL`，因此 PSRAM
或 system-heap 注册失败本来就会通过 `apret` 让 platform init fail closed。板测完成后
的只读审查指出：未来若纯 CP profile 单独启用同一 role-local Kconfig，原实现可能只
记录失败日志。最终工作树补了一条通用最终返回值传播；它不改变 generation 149 已
验证的成功路径，也不改当前 xTS 的既有失败语义，但不冒充已重新下载的产物。

该复核改动后的 direct clean build 已完成 CP/AP/BL1，结果 PASS：CP identity
`bk7258-role-48b23aed00182354`、config
`c44848033d2a0070a9ed561d0262cf3cdc04750f995e88aec15ad5a2e3cde6ec`；AP identity
`bk7258-role-1896114fbe74e630`、config
`6dba42d3232a8b42820744252d4e89d92254b146c2e95be0a1e846c8ae289c9f`。
没有为这条未来失败路径再次全量下载；下一次下载仍必须使用更高 generation/counter
和两把全新的 P-256 密钥。

## 4. 新密钥、签名包与物理镜像

generation 149 分别新建 BL1 和 MCUboot P-256 密钥，未复用 generation 147/148 或
任何历史私钥。验收后已删除临时私钥目录，只保留公开指纹：

| 信任根 | public SPKI SHA-256 |
|---|---|
| BL1 | `50610152cf8edf9ee14192c875809c1b4e18f0b42a9de216a9673ff9ba0d5173` |
| MCUboot | `8a2952b164d42ef2b1d45e5a569ed5ff211c5b942c73d59f98453887d3da85bc` |

| 产物 | 值 |
|---|---|
| version / MCUboot / BL1 counter | `1.88.5+149` / 149 / 149 |
| signed package | 8 images；`9b8c51c93380aeceb12a5d4961448bd01039b062e89f77eeedfd44a1ba7d344f` |
| operator image | 8,364,032 bytes（`0x7fa000`）；`fc3ec546399965c4dd43ef2d9d5bd2a33ab608e562d3befbe2c4f3f42a5da11c` |
| preserved Agent persistent | offset `0x561000`，2,723,840 bytes；`ef3393a63494d5bb6883ff8455d2803ef94e8d9379c48bc637ad4a0c9f1a347f` |

签名包的结构、BL1/BL2/CP/AP 公开信任链和 flash contract 均 PASS。operator image
由 generation 148 下载后读取、仅作为保留区来源的基础镜像物化；这不表示缩栈方案
通过 xTS 验收。该基础镜像 SHA-256 为
`3691dbfc32bc0e7313baeda6bbb9a117586df6316d0678bc472a99b56a2c9c85`；
`usr_config` 与其逐字节一致，完整 Agent persistent payload 得到保留，
`[0x7fa000,0x800000)` immutable tail 未进入输入。

主要物理 image 身份：

| Image | offset / size | SHA-256 |
|---|---:|---|
| boot | `0x000000` / 69,462 | `ada917fa88287eb8a9489ed2971c76764fb01d517a1f4c382eaaac4f7b516d86` |
| CP | `0x011000` / 1,392,640 | `a0ea6abfe501384f7cf9f081503ba8883b26455981abe5ccb2276f8dcfccb60d` |
| AP | `0x165000` / 1,183,744 | `2caff32381b183a78b5fa028b126f687b82496d0ceb4a594288ac30eff040d86` |
| CP/AP pair | `0x286000` / 2,576,384 | `92aad2221c937647fb8c2bee32a36dde233318a34026fc25749bc6adb5730871` |

其余 package member：manifest A
`1044dbbb865c105b36a857a0195bbd155cff9595cccd7ee2b835323acbcf1a0d`、manifest B
`d17a0342e7ee42c95b689536acc813fefdeccb5c8ee8cc58c6e27786e15d131f`，以及大小均为
14,586 bytes 的 BL2 A/B
`4b545685e92161bd2dc9ad7e04b1b79e6293d3ea0f80edcd58ca926c2abf317f`。

## 5. COM3 全量下载

2026-08-27 11:57:57～11:59:40 直接调用 BK Loader 2.1.11.15。命令只有一个
`[0]` 输入：`operator-g149.bin@0x000000-0x7fa000`；loader 记录文件 CRC
`0xee71e4ff`，并依次输出 `EraseFlash ->pass`、`WriteFlash ->pass`、
`Enprotect pass`、`Writing Flash OK` 和 `All Finished Successfully`。

Windows loader 最终进程退出码仍为已知兼容性值 1，但完整写入阶段、签名启动和后续
功能门禁均通过，因此不重复写入。没有执行 chip erase、第二输入、tail、OTP/eFuse、
lifecycle、校准区或 debug lock 写操作。

## 6. Generation 149 板端结果

| 项目 | 当前代结果 | UART raw SHA-256 |
|---|---|---|
| 启动链 | BL1 primary、BL2 AP、CP/AP PSRAM、`rc.sysinit`、final-init、`rcS`、NSH、Agent ready；`BPSR SYSTEM HEAP PASS size=65536` | `7fbd7621af9705cf9f04df88c12e2c0412231b8a6701430a6dd55adf05db0a65` |
| 基线 | `help`、`ps`、`free`、`irqinfo`、`dumpstack` PASS；Umem total 183,192、free 87,560、maxfree 68,840 bytes，比 g148 total 精确增加 65,536 bytes | `c033fd7222f8f970b9da15e77cfb668a1775337448320cc98407018c58e72114` |
| `cmocka_mm_test` | 8/8 PASS | `c8905dc54c88e01e2ef11bd1d518fb0d2ccd7686960aaf4d0703dc28ed788a63` |
| `cmocka_sched_test` | 16/16 PASS，保持 16 KiB runner stack | `9a222b3fdca609fc56bed3b7c04fd39fb9edac68da064701ca325ea79d559ede` |
| `ostest` | 退出状态 0；执行后 task/heap 健康 | `3e37633b3739edd0387ea7d29cb875287ac3601873a80a6b7ef83a5640588f02` |
| `getprime` | thread 0 找到 1,230 个素数，最后为 9,973，用时约 1.678 秒，状态 0 | `bfa33c8834c57211d78b012df2239616f0dd10f713dd66109e854d5836a98678` |
| `mm` / `ramtest` | heap `TEST COMPLETE`、状态 0；4,096-byte marching/pattern/address tests 无 error、状态 0 | `48b11ce120765b483af9786d33055b829df26ce0b38c830d42082fb6898a2ca1` |
| `scanftest` | 临时 RAM tmpfs 上 `OK: 164, FAILED: 0`、状态 0，随后删除并卸载成功 | `581579027b9e1e5773cf93ed0fc021cfa8c3129d2b1b68fa774a67d8fec8ab3c` |
| `hello` / `pipe` | hello 状态 0；FIFO interlock、FIFO、PIPE redirection、PIPE 全部 PASS，状态 0 | `95e55ae3d0fdb88ca93de0c56a5830b9d852b682b73f84e81f257fd27b6fba13` |
| 最终冷启动 | CP system heap、双核启动、final-init 和 Agent ready 再次通过；Agent ready 约 5.889 秒，无 Panic/Fault/assert | `d2a09f043f076259cbdf4f2a92ed595415ecba84e0b6676a23a1981035bcfb70` |

### 6.1 `ostest` 证据边界

`ostest` 的大量子项输出已被首次 session 捕获，但最终完成行恰好落在串口 capture
切换间隙，不能伪装成直接捕获到该文本。限时等待后，SWD 只读样本显示 PC 位于
`bk7258_idle_wfi`、没有异常；随后同一个 NSH 立即执行 `echo OSTEST_STATUS=$?` 得到
0，`ps/free` 也健康。因此本项按“完整执行输出 + idle 样本 + 同 shell 退出状态”的
组合证据判 PASS，并明确保留 capture gap 这一限制。

### 6.2 `scanftest` 夹具

第一次直接在 pseudo-root `/tmp` 执行得到 `OK: 94, FAILED: 2`，根因是该路径不能创建
普通文件，不计入验收。随后只在 RAM tmpfs 上重跑，得到 164/0；测试后删除文件、
卸载 tmpfs 并确认路径消失。没有写入 Agent persistent 或 TF 用户数据。

## 7. 与 generation 143 证据的关系

generation 149 关闭的是当前代核心 general-app xTS 和 CP heap 容量问题。generation
143 已通过的符号化 Backtrace/Allsyms、critmon 可观测性、cpuload、Trace 和 10 秒
memstress 仍是这些专项的有效证据；本轮没有为了重复同一门禁而再次执行。两代证据
分别对应相同维护 profile 的不同验收范围，不能互相改写产物身份。

## 8. 未覆盖、隔离与延期项

- `fstest`、`blktest`、`opus_ramtest` 和其他可能改写介质的用例未执行；必须使用专用
  可恢复介质，不能触碰 Agent persistent 或用户 TF 数据。
- GPIO/UART 物理 loopback、AP RTC/timer/RNG、`driver_test` 和 LIBCXX 仍需对应夹具或
  独立 profile 后再验收。
- WDT/reset 类用例、受控 fault 和 critmon 非零告警阈值必须作为可恢复专项执行，不能
  混进普通产品数据盘回归。
- 12 小时稳定性 soak 经 owner 于 2026-08-27 明确延期；后续恢复时要使用产品
  profile、独立连续日志、明确的内存/任务/网络/Agent 健康判据。

## 9. 最终状态

- 现役 host fixture：PASS（公共门禁 + 281/281 cmocka）；
- generation 149 clean build、新密钥、公开信任链、签名包和 flash contract：PASS；
- COM3 单一 `0x7fa000` operator image 全量下载与保护边界：PASS；
- 当前代非破坏性 xTS 核心：PASS；
- P0 全部官网项目：PARTIAL，剩余项见 §8；
- 板上当前镜像：xTS generation 149（`1.88.5+149`）。
