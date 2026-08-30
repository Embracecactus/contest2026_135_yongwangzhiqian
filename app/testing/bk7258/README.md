# BK7258 OpenVela target tests

This directory is the canonical contest-repository source for an official-form
NuttX/OpenVela CMocka application.  The team manifest maps the whole directory
to `apps/testing/bk7258`.  The official `apps/testing` Kconfig, Make and CMake
entry points discover direct child applications automatically, so no source
file in an official repository is modified.  The same directory can later be
submitted upstream at that location without a product-only build wrapper.

Each maintained board has an `xts` CP profile paired with its normal
`openvela_ap` profile.  After downloading that pair, run this program from the
CP NuttShell:

```text
cmocka_bk7258_board_test
```

The same application covers `t5_board`, `t5ai_core`, and `aidk_ai_toy`; its
board identity comes from the selected Kconfig, not a test-build default.  The
tests use the public UART device and versioned AP lifecycle/supervisor APIs.
They do not call Beken SDK functions and do not reach into AP-owned peripheral
drivers.  Board-specific AP boot evidence is selected explicitly by the linked
official pytest case under `tests/pytest/test_bk7258`.
