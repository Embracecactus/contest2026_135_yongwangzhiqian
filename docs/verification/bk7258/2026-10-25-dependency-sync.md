<!-- SPDX-License-Identifier: Apache-2.0 -->
# 2026-10-25 固定依赖同步与文档事实归位

本次范围为工作区依赖同步、源码合同核对和相关文档修正。团队 HEAD 为
`930d75ae34f215dcaf8707751d5e27cc59ec7ca9`；已有 7 个修改/新增文件保留，
包括唤醒词训练工作和 `SOURCE_PROVENANCE.md` 的未提交补充。
没有修改兼容性合同、驱动或产品代码，没有执行固件构建、签名、烧录、
手机仪器测试或真实云调用。板级当前状态只在 `boards/bk7258/CONFIGS.md` 维护。

## 同步前与同步后

| 输入 | 同步前实际检出 | 团队 manifest 固定值 / 同步后实际检出 |
|---|---|---|
| NuttX | `5ecdd18b230acd2a86068cb4559b9f3f59363857`，另有两处本地 HCI 修改 | `892be7ba7dc468f11f32eade4c553cc76b0219c3` |
| Agent | `35df4b347b17f07c972eb6e7e14a14fee1be78e4` | 相同，未重新同步 |
| Beken SDK | `cb080de1655d579c7593ecf504c440997c4c137b` | `eb0d4252a83f0ba88f0eb947fcfa2e36cbcb9bb4` |

NuttX 远端 `Embracecactus/nuttx` 的 `fix/bk7258-review-lifecycle` 实际返回
上述固定提交。SDK 固定对象也经原 fork 获取。旧 NuttX 的 `audio/audio.c`
SHA256 为 `d39e1586a2a54bf510d7c98688987b2488393e453053fb31623ff5521023c57c`，
`drivers/video/v4l2_cap.c` 为
`b43111397895b3103aee0a9863600deafcb88bb2caede6980b6cf8f2bf570c55`。
直接调用构建所用的 `kernel_compat.verify_sources()`，同步前实际拒绝 HEAD
不匹配；未调用构建命令来尝试越过该失败。构建入口在配置和编译前调用此校验。

源码核对确认：旧 `audio_close()` 在 `spin_lock_irqsave()` 保护内调用
lower shutdown；团队 `bk7258_aud_shutdown()` / `bk7258_mic_shutdown()`
在 `close_safe` 未置位时进入含 `nxmutex_lock()` 的清理路径。
同步后的 upper half 先释放 spinlock，再执行 `audio_shutdown()`，与 lower half
的已发布生命周期合同对齐。这是源码检查，没有用实板触发或排除死锁。

## 保存现场与定向同步

本地 `.repo/manifests` 仍有独立历史，未覆盖或重置它。新增工作区本地文件
`.repo/local_manifests/bk7258-reviewed-dependencies.xml`，从团队 manifest
逐项复制 NuttX、Agent、SDK 三个已发布固定值及 remote/upstream；只同步
实际缺失的 NuttX 和 SDK。该文件是当前团队声明的本地映射，基础 manifest
更新到相同声明后可移除，不是另一套依赖版本来源。

先执行普通 `repo sync --network-only --no-manifest-update --current-branch
--no-tags --no-prune -j2 nuttx vendor/beken/bk_avdk_smp`。取得对象后确认两个
本地 HCI 文件与发布提交逐字节相同，再仅 stash 这两个文件，随后执行
`repo sync --local-only --no-manifest-update --no-prune -j1 nuttx
vendor/beken/bk_avdk_smp`。两步退出码均为 0；没有 force-sync、强制 checkout
或全工作区同步。

原两文件、二进制 diff、HEAD/status 与前后有效 manifest 均保存在工作区
`out/bk7258-dependency-sync-20261025/`；保留的 HCI stash 对象为
`61206dbbc675ad905f46727fad1f110375cf8f26`。原 diff SHA256 为
`ccd7fea4fc24d4face505174f7433836a1ba583f2e73d8443293fffcf040beb6`。
同步后两个 HCI 文件内容与同步前相同，已成为固定提交内容，不需再应用补丁。
OpenAMP 手工目录、SDK 未跟踪缓存和原有团队改动均保留。

## 已执行验证与边界

- 同步后原 `verify_sources()` 通过 HEAD 及全部 18 个文件摘要。
  `audio/audio.c` 为
  `fed6f6a3a3853d64bd0ea005c51ccee4b11ad7c7c1d8f3d4964a4c3f87139f70`，
  `drivers/video/v4l2_cap.c` 为
  `cb6a0239ab1894dc675bf5645a466f34a835af97cb61648c1472bbf727e9530d`。
- `python3 tests/host/bk7258/test_kernel_compat.py`：6 项通过。
- 既有 `test_audio_close_lifecycle.py --nuttx ../nuttx` 通过：从实际 NuttX
  提取函数，覆盖回调锁序、关闭失败、重新打开、共享缓冲回收与多次打开。
  锁、lower half 和分配器使用主机 mock，不代表硬件停止时序通过。
- 既有 `test_v4l2_cap_error_propagation.py --nuttx ../nuttx` 通过；
  `test_sdio_dma_completion.py --sdk ../vendor/beken/bk_avdk_smp` 通过，
  覆盖实际固定 SDK ISR 在 GDMA 开启/关闭时的完成中断判定。
- 本次 8 份变更文档中新增的 13 个本地链接/锚点检查通过；Master Plan 改名
  的历史小节无受影响入链。差异空白检查通过。
- 前后哈希核对证明原有 7 个团队修改/新增文件未被改写；NuttX 和 SDK 没有
  tracked 差异，已有未跟踪目录保留。
- `930d75ae` 自身提交说明记录三板 CP/AP 与未签名 BL1、五套 SDK、主机检查、
  33 项 Android 单测及两类 APK 构建通过；本次没有重跑这些构建。
  同步不能升级为完整 manifest 独立复现、当前固件或实板验收结论。

## 历史功能证据的修正依据

本次读取了 [2026-10-18 验证记录](2026-10-18-stability-and-phone-hil.md)
的后续章节，并核对原始结果；不能只用该文件开头的早期快照概括全天：

- 后半段真实任务结果 `process_returncode=0`、`device_result=confirmed`；
  独立任务超时取消也保留进程结果。`feedback_verified=false` 仍保持原值，
  LCD 提交由同窗口的串口证据分别支撑。
- 后半段单帧为 13,575 B、640×480，512 B 分块，传帧 2.240468 s、总计
  5.257466 s，释放确认且随后缓存无效；早期 15,211 B / 76.6 s 是另一轮。
- 真实浏览器结果包含 15,247 B 取景、取消及随后 15,055 B 新帧，解码为
  640×480。原结果文件 SHA256 为
  `f69108e0e265be8fc8e682e4bf883fe8afd7b31cccc99fad8cd1922d46434d06`。
- 用户看到表情变化的确认不等于完整动作关联或马达体感通过；缺实体 NFC 卡
  仍限制卡片场景验收。770 的 OTA 后重连失败与后续超时保留，不引用 634 或
  767 的成功覆盖失败。

上述是既有版本证据，不是本次新做的设备测试。
[2026-10-20 相机候选](2026-10-20-workbench-camera-lifecycle.md)的两次连接停在
TLS、未发相机命令，及其本地保存的主机/合成浏览器验证边界均保持。
README、中英文平台入口和历史 Master Plan 改为引用唯一板级现状，不删改旧验收记录。
