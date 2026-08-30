# Verification: BK7258 unified OTA foundation

- Date and time zone: 2026-08-21 Asia/Shanghai
- Verifier: Codex
- Source state: uncommitted worktree on `18510b9`
- Environment: official Arm GCC 10.3-2021.10, manifest-pinned BK7258 SDK

## Result

**T5AI_CORE_BUILD_PASS / T5BOARD_BUILD_PASS / AIDK_BUILD_PASS /
HARDWARE_PENDING.**

The replacement Pair Installer, Boot Control, signed catalog, AP OTA Manager,
file/HTTPS sources and versioned AP-to-CP RPMsg transport compile after the
OpenVela-native chip/board directory migration and complete SDK rebuild.

## Build evidence

All commands used `tools/bk7258/bk7258.py build --boot mcuboot --clean`, the
build-pinned public roots, `/usr/bin/openssl`, eight jobs and rollback floor 3.
All six CP/AP CMake compiler records identify GCC 10.3.1. No test target ran.

| Board | Layout | Result |
|---|---|---|
| T5AI-Core | `bk7258-091dd617ad7d1d49` on-chip persistent | PASS |
| T5Board | `bk7258-5641c11040abf787` removable block | PASS |
| AIDK AI Toy | `bk7258-381e2cdd1286ac59` fixed block | PASS |

Every resolved CP/AP configuration contains `CONFIG_BK7258_OTA_RPMSG=y` and
every AP configuration contains `CONFIG_BK7258_OTA_MANAGER=y`. T5Board also
builds file and HTTPS sources; AIDK builds the file source. T5AI-Core HTTPS
selection remains a later configuration step. BL2 copy size is 13,632 bytes.

The AP SDK bundle was fully rebuilt from
`Embracecactus/bk_avdk_smp@cb080de1655d579c7593ecf504c440997c4c137b`.
Its accepted tree is
`4eac7a1f2f744292ff496b9780da4e981b4fe62627652923e672e6bab6997134`;
the profile omits vendor startup, CLI, lwIP/netif, historical OTA, RTOS and
duplicate MbedTLS ownership while retaining the radio/mailbox data plane. The
CP bundle remains
`1eda61d23f80e5c1f598b80facd174ba2337c80d5a75853a3ffacab9e7a9fe05`.

## Hardware boundary

No firmware was downloaded in this checkpoint. A new package must be signed by
the already provisioned development roots before the T5Board can execute the
new AP Manager/RPMsg path. Public roots were reconstructed only from the
previous verified package; no private material was searched, read or recorded.

## Residual scope

- A current-root private MCUboot signing input is required to create the first
  installable pending CP/AP package; no private path or content was searched.
- File and HTTP sources and AP-to-CP staging remain runtime-unverified.
- T5AI-Core HTTP selection, TF/NAND resume, BLE, UART/USB, automatic health
  confirmation and resource/delta installers remain incomplete.
