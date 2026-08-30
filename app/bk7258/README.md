# BK7258 board diagnostic built-ins

映射到 openvela `apps/system/bk7258`，由官方 `apps/system/`
CMake、Kconfig 和 Make 递归机制自动发现。
本目录只承载由 App Kconfig 显式选择的 BK7258 NSH 维护与诊断命令；初始化模板
`app/hello_app` 保持独立且不承载产品功能。每个命令都有独立的
`CONFIG_BK7258_APP_*` 开关；底层 Driver/Test
符号只负责能力/端点，不再自动注册应用。

可用 App 开关：

```text
CONFIG_BK7258_APP_BKVALIDATE
CONFIG_BK7258_APP_APCTL
CONFIG_BK7258_APP_RPMSG_TEST
CONFIG_BK7258_APP_RPMSGFS_TEST
CONFIG_BK7258_APP_BT_IPC_TEST
CONFIG_BK7258_APP_WIFI
CONFIG_BK7258_APP_PSRAM_TEST
CONFIG_BK7258_APP_GPIO_TEST
CONFIG_BK7258_APP_GPIO_IRQ_TEST
CONFIG_BK7258_APP_IRQ_TIMER_TEST
CONFIG_BK7258_APP_TIMER_SELFTEST
```

每个开关还带有 `_PROGNAME`、`_PRIORITY`、`_STACKSIZE` 子配置，可在
`menuconfig` 中调整。App 的 `depends on` 保证底层能力不满足时命令不可选。

N14 `cp_nsh_psram + ap_smp_psram`新增：

```text
bkpsramtest info
bkpsramtest heap [iterations=16]
bkpsramtest all  [iterations=16]
bktimertest [iterations=64]
```

`bkpsramtest info`同时核对CP容量/heap/MPU、boot-only raw gate和AP双核启动门禁；`heap/all`
只测试当前CP private heap。全容量破坏性PSRAM测试只在启动时、建立heap和释放AP之前执行，
不存在运行时raw命令。`bktimertest`验证SDK software timer callback的task context、callback内
self-delete及queued final-free。

完整范围、源码约束和实板证据见：

- [N14 board verification](../../docs/verification/bk7258/2026-08-03-n14-psram-board-verification.md)
- [N14 source verification](../../docs/platforms/bk7258/nuttx-port/n14-psram-source-verification.md)
- [N14 evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md)

P5 validation skeleton (opt-in with `CONFIG_BK7258_APP_BKVALIDATE=y`) exposes:

```text
bkvalidate list
bkvalidate run <descriptor-id>
bkvalidate all-compatible
```

The target-side table in `bkvalidate_main.c` is the sole descriptor source.
`all-compatible` serializes global resource claims and emits `SKIP` for interactive, fixture,
destructive-fault, planned, or unavailable requirements.  The dispatcher does
not call vendor SDK functions directly.  Individual descriptors may invoke
explicitly selected BK7258 diagnostic endpoints; they are never started merely
because the dispatcher is enabled.

## 测试分层约定

- 纯逻辑（无硬件依赖）的 C host 用例放 `tests/host/bk7258/`，唯一完整入口是
  `make -C tests/host/bk7258 check`。旧的维护工具 Python 测试目录已经退役。
- 命令壳（NSH 内置命令）留在 `app/bk7258/`，由 `CONFIG_BK7258_APP_*`
  门控；每个命令只有 enable/PROGNAME/PRIORITY/STACKSIZE 和依赖声明。
- CP/AP 公共生命周期契约通过 `app/testing/bk7258/` 的官方格式 CMocka 板上应用执行；
  三块板的 UART0 自动化由 `tests/pytest/test_bk7258/` 链入官方 pytest。需要显式操作
  硬件的 rpmsg / gpio / psram / bt / irq / timer 命令仍保留
  `CONFIG_BK7258_APP_*` 形态，再由 pytest 调用，不在测试中复制产品实现。
