# BK7258 SDK 静态库生命周期

SDK 源码版本和 OpenVela ARM 工具链都由 team manifest 固定。唯一维护者入口是：

```bash
tools/bk7258/bk7258.py sdk list
tools/bk7258/bk7258.py sdk verify --profile cp
tools/bk7258/bk7258.py sdk install \
  --profile cp --bundle PATH [--replace]
tools/bk7258/bk7258.py sdk rebuild \
  --profile cp --source <clean-sdk-checkout> --jobs 8 [--replace]
```

`--source` 必须是 team manifest 指定 commit 的 clean checkout。工具链来自 manifest
项目 `prebuilts/gcc/linux-x86_64/arm-none-eabi`，不接受 PATH 或命令行覆盖。

跟踪的 profile 位于：

```text
board/bk7258/bk_idk/sdk-profiles/<manifest-version>/
  cp.config
  ap.config
  ap-sdio4.config
```

每个 profile 只声明 SDK 配置差异、NuttX 已拥有而需排除的链接输入，以及一个接受的
bundle tree SHA-256。本地 bundle 被 Git 忽略：

```text
board/bk7258/bk_idk/armino_as_lib/versions/<version>/<profile>/
  config/
  include/
  libs/
```

`sdk rebuild` 在临时 clean clone 中调用官方 SDK，读取实际 `app.elf` link command，
提取完整静态链接闭包，按 profile 排除 NuttX runtime，并重编 UART object加入
`CONFIG_BK_PRINTF_DISABLE`。bundle 与 profile hash 在同一锁内原子替换；失败恢复旧
bundle和旧 hash。

没有独立 manifest/provenance、registry、set/lock、版本 Python 常量、角色库名清单或
legacy fallback。CP/AP/OpenVela build直接校验并消费对应 profile bundle。
