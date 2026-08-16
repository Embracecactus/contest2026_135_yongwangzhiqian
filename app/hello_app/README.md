# BK7258 board diagnostic built-ins

映射到 openvela `packages/demos/contest2026_135_hello_app`。
本目录保留最初的 `hello_app`，同时承载各 Stage 由 App Kconfig 显式选择的 NSH
诊断命令。每个命令都有独立的 `CONFIG_BK7258_APP_*` 开关；底层 Driver/Test
符号只负责能力/端点，不再自动注册应用。

可用 App 开关：

```text
CONFIG_BK7258_APP_HELLO
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

- [N14 completion](../../docs/bk7258-t5ai/nuttx-port/prompts/14-n14-psram.md)
- [N14 source verification](../../docs/bk7258-t5ai/nuttx-port/n14-psram-source-verification.md)
- [N14 evidence index](../../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md)

P5 validation skeleton (opt-in with `CONFIG_BK7258_BKVALIDATE=y`) exposes:

```text
bkvalidate list
bkvalidate run <descriptor-id>
bkvalidate all-compatible
```

Descriptors are versioned in
`tools/bk7258/bk7258_validation_descriptors.json`.  `all-compatible`
serializes global resource claims and emits `SKIP` for interactive, fixture,
destructive-fault, planned, or unavailable requirements.  The runner core uses
only public device-path APIs; it does not call vendor SDK functions or start
the legacy production validation workers.

## 测试分层约定

- 纯逻辑（无硬件依赖）走 host 测试：C 用例放 `tests/bk7258/`
  （`Makefile` + `run_tests.sh`），Python 用例放 `tools/bk7258/tests/`。
- 命令壳（NSH 内置命令）留在 `app/hello_app/`，由 `CONFIG_BK7258_APP_*`
  门控；每个命令只有 enable/PROGNAME/PRIORITY/STACKSIZE 和依赖声明。
- 硬件验收命令（rpmsg / gpio / psram / bt / irq / timer 等）不接入 cmocka：
  cmocka 是目标端自测框架，接入后 host CI 仍无法执行，反而把 contest 仓应用
  耦合到公共 apps 测试框架。硬件验收以 `CONFIG_BK7258_APP_*` 命令形态保留。
