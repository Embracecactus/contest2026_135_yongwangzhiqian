# 下一阶段恢复提示词（2026-07-15）

把下面整段发给 Claude，用于 `/clear` 后恢复上下文。

```text
我们在 openvela 2026 大赛仓库继续开发，请严格遵守：

- 只改 `$CONTEST/` 队伍仓。
- 不直接改外层 `nuttx/`、`apps/`、`packages/`、`vendor/` 等官方 checkout。
- 当前赛道是“新硬件适配”，目标是 RV1126B 自有板 HPMCU 侧 openvela / NuttX BSP。
- 官方已确认：RV1126B 自有板可以作为适配目标；队伍仓 overlay + manifest linkfile 方式符合要求，不需要 SCM 单独开仓。
- 使用苏格拉底式交流：遇到没有对齐官方指导或用户规则的地方，立刻停下问我。
- 不要全量扫描代码；需要理解代码时先用 CodeGraph，且尽量只看 `contest2026_135_yongwangzhiqian/board/contest_board/` 和明确点名的文件。
- 根目录 README 现在暂时保留官方指导模板，最终所有功能/验证/日志整理完成后再改成作品说明；不要现在替换 README。

当前关键状态：

1. Contest 仓：
   `$CONTEST`

2. 当前 contest HEAD：
   `ec43ebb`

3. 当前工作区预期有修改：
   - `README.md`：暂时是官方模板/指导，不要现在改成作品 README。
   - `board/contest_board/configs/nsh/defconfig`：新增了：
     - `CONFIG_INTELHEX_BINARY=y`
     - `CONFIG_RAW_BINARY=y`
     这两个要保留，用于生成 `nuttx.hex` / `nuttx.bin`。

4. 已经尝试过启用 `ps`，但后来决定回退，不把 `ps` 作为当前 L0 基线条件：
   - 不保留 `# CONFIG_NSH_DISABLE_PS is not set`
   - 不保留 `CONFIG_FS_PROCFS=y`
   当前记录里 `ps: command not found` 是已知事实，不当作失败。

5. 最新验证文档已经写入：
   `docs/verification/2026-07-15-rv1126b-ec43ebb-amp-nsh-baseline.md`
   先读它，不要重新推理全部历史。

6. 2026-07-15 已验证事实：
   - openvela / NuttX 构建生成：
     - `$WORKSPACE/nuttx/nuttx`
     - `$WORKSPACE/nuttx/nuttx.bin`
     - `$WORKSPACE/nuttx/nuttx.hex`
   - `nuttx.bin` 大小 79K / 80320 bytes，sha256：
     `0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075`
   - SDK 路径：
     `$SDK`
   - SDK 已完整编译过；复测只需要替换 HPMCU 镜像，不需要从头编 SDK。
   - 包装链路：
     `nuttx.bin -> SDK/output/rtt.bin -> mkimage + output/amp.its -> output/firmware/amp.img -> ./build.sh updateimg -> update.img`
   - 生成的 `amp.img`：
     `$SDK/output/firmware/amp.img`
     sha256：`35f501c77ac77a27437160b8de190e9245ef736ee5b1d1d89634d28592618feb`
   - 生成的 `update.img`：
     `$SDK/output/update/Image/update.img`
     sha256：`89171c596e0df878ab0efa44d0f22007decc537853dae08281fee649fbe628a0`
   - 本次板测只烧录/更新了 `amp.img`，没有完整烧录 `update.img`，因为完整烧录耗时；文档中要如实说明。
   - 板测串口验证通过：
     ```text
     nsh> uname -a
     NuttX 0.0.0 e02f581e23 Jul 15 2026 14:00:58 risc-v rv1126b_evb
     nsh> help
     ... 命令列表正常输出 ...
     nsh> ps
     nsh: ps: command not found
     nsh>
     ```
   - 因此当前 L0 基线结论是：NSH prompt、UART RX/TX、`uname -a`、`help` 通过；`ps` 未启用，不作为本次基线通过项。

7. SDK 侧必要前置条件：
   - Linux A 核不能占用 HPMCU 控制台使用的 UART5 M0。
   - UART5 M0：GPIO4_PA6 / GPIO4_PA7，FUNC5，1.5M 8N1。
   - SDK 中有必要的自有板 DTS / SportCam 改动，例如 `rv1126b-sportcam.dts`、`FET1126B-S.dtsi`、`rv1126b-amp.dtsi`、`device/rockchip/.chips/rv1126b/amp_mcu.its` 等。
   - 这些 SDK 改动不属于 contest 仓主体代码，但必须在最终复现文档中说明，最好后续整理为 patch 或详细 diff 文档。

下一阶段建议任务：

A. 先确认当前 contest 仓状态：
   ```bash
   git -C $CONTEST status --short
   ```
   预期至少有：
   - `M README.md`
   - `M board/contest_board/configs/nsh/defconfig`
   - 新增文档：`docs/verification/2026-07-15-rv1126b-ec43ebb-amp-nsh-baseline.md`
   - 新增文档：`docs/next-stage-prompt-2026-07-15.md`

B. 不要马上改 README。

C. 下一步优先整理 SDK 侧可复现说明或 patch 文档，目标文件可以是：
   `docs/rv1126b-sdk-integration.md`
   内容应说明：
   - SDK 路径
   - 为什么 Linux A 核要避让 UART5
   - 哪些 DTS / defconfig / package-file / amp_mcu.its 改动是必要条件
   - 如何从 `nuttx.bin` 生成 `amp.img`
   - 如何 `./build.sh updateimg`
   - 本次只烧录 AMP 分区与完整 update.img 的区别

D. 后续发布前可补一次完整 `update.img` 整包烧录验证；但不要在没有用户确认的情况下启动耗时烧录。

E. 最终提交前再改根 README，写成作品说明，并明确：
   - 作品方向：新硬件适配
   - 作品主体：`board/contest_board/`
   - `app/hello_app` 和 `quickapp/hello_quickapp` 是保留模板，不是作品主体
   - 构建、SDK 打包、烧录、串口验证步骤
   - AI Coding 日志位置和使用说明
```
