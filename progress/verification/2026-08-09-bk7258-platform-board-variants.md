# BK7258 platform and physical-board variant verification

Date: 2026-08-09

## Scope

This gate verifies the source-layout migration from `board/bk7258_t5ai` to
the generic `board/bk7258` platform and the build-time selection of two
physical wiring descriptions:

- `boards/t5ai_core`: T5AI-Core V1.0.1, compatibility default;
- `boards/t5_board`: T5-Board V1.0.2, explicit Kconfig selection.

No NuttX or official SDK source was modified.  Both variants continue to use
the immutable BK7258 SDK v3.1.1.9 wrapper bundle.

## Build evidence

The existing fully expanded CP/AP drivercheck-MCUboot configurations were
used because a clean Kconfig refresh is currently blocked by unrelated
workspace Kconfig syntax/parser incompatibilities.  No alternative defconfig
or test script was added for this migration.

| Physical board | Role | Result | Postbuild output |
|---|---|---|---|
| T5AI-Core V1.0.1 | CP | compile/link/postbuild PASS | `app.bin=218260`, `app_crc.bin=231914` |
| T5AI-Core V1.0.1 | AP SMP | compile/link/postbuild PASS | `app1.bin=180360`, `app1_crc.bin=191658` |
| T5-Board V1.0.2 | CP | compile/link/postbuild PASS | `app.bin=218260`, `app_crc.bin=231914` |
| T5-Board V1.0.2 | AP SMP | compile/link/postbuild PASS | `app1.bin=180360`, `app1_crc.bin=191658` |

For the explicit T5-Board build, Make resolved
`BK7258_BOARD_VARIANT_INCLUDE` and all C/C++ preprocessor flags to
`board/bk7258/boards/t5_board/include`.  With no explicit board symbol, the
build resolved the corresponding Core directory.

## Boundary

This is a source, configuration, compile, link and package gate.  T5AI-Core
retains the existing board evidence.  T5-Board pin mappings are derived from
the owner-supplied V1.0.2 schematic but are not yet physical-runtime proof.
GPIO, TF, RGB LCD and DVP validation must be performed independently with
pin-compatible profiles.

The durable naming and ownership decision is recorded in
[ADR-023](../../memory/decisions/ADR-023-bk7258-platform-board-variants.md).
