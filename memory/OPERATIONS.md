# Operations

Last reviewed: 2026-08-03

Do not place credentials, tokens, private keys, or sensitive production data in this file.

## Environments

- Workspace root contains official `nuttx/` and `apps/` siblings plus this contest repository.
- Active physical target: Tuya T5-AI/BK7258; current workstation mapping used COM7 for download/reset and COM11 at 460800 8N1 for UART. Rediscover ports before each session.
- Default SDK bundle: `board/bk7258_t5ai/bk_idk/armino_as_lib/versions/v3.1.1.9` (ignored, checksum-pinned).
- Matching SDK source is external and read-only; supply it through `BK7258_SDK_SOURCE` for source verification.

## Required verification

- Run the stage-specific source/ELF verifier and existing RPTUN/BLE/packaging gates.
- Run CP and AP SDK bundle `--check` for the selected version.
- Require `git diff --check`; confirm official `nuttx/` and `apps/` tracked diffs are zero.
- For a completed hardware stage, retain raw UART/J-Link logs, artifact hashes, physical reset evidence, and regression tests proportional to the change.
- Canonical N14 matrix: [N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).

## Build and release

- Build paired CP/AP profiles with `board/bk7258_t5ai/scripts/build_dual_image.sh`; N14 uses `cp_nsh_psram + ap_smp_psram` and v3.1.1.9.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP feature-profile pairs and runs post-link verification.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- Prefer three-segment sparse flashing for normal updates because it preserves LittleFS and calibration data.
- `all-app-factory.bin` is destructive to the data/calibration area. Use it only for an explicitly approved factory/first-calibration gate.
- A flash PASS is not sufficient: require a new serial capture, `PASS_NSH`, and the stage-specific health command.

## Rollback and recovery

- N13 `cp_nsh_ble_gatt + ap_smp_ble_gatt` is the no-PSRAM BLE rollback pair.
- The immutable pre-N14 source rollback point is commit `c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0`.
- Recover a nonbooting board with the known Tier-1/minimal bootloader and documented sparse segments; do not erase broad ranges by inference.

## Observability and support

- CP NSH commands include `apctl`, `bkrpmsgtest`, `bkrpmsgfstest`, `bkbttest`, `bkpsramtest`, and `bktimertest` under matching profiles.
- Use raw UART logs as evidence; use J-Link only for bounded register/memory inspection and avoid leaving diagnostic telemetry in the final image.
- Store summarized, non-sensitive evidence routing in `progress/verification/`; keep full raw logs in the canonical stage log tree.
