# BK7258 board diagnostic built-ins

映射到 openvela `packages/demos/contest2026_135_hello_app`。
本目录保留最初的 `hello_app`，同时承载各 Stage 由 Kconfig显式选择的 NSH诊断命令。

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
