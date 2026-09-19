---
name: bk7258-hil-download
description: Profile, preflight, run, and verify BK7258 direct/full or bounded multi-segment downloads with Beken BK Loader, then hand off reset-synchronized UART/J-Link evidence collection to windows-hardware-debug. Use for AIDK AI Toy, T5-Board, T5AI-Core, or a newly reviewed BK7258 board profile when an exact artifact, physical target, and COM route must be handled reproducibly. Do not use for unknown boards, OTP/eFuse/security writes, unverified package trust, or as a substitute for signed OTA deployment.
---

# BK7258 Profiled HIL Download

Use this skill as the BK7258 Flash transport layer. Keep generic Windows UART,
reset, J-Link, and BLE work in the sibling `windows-hardware-debug` skill. The
two skills compose; neither silently broadens the other's safety boundary.

## Workflow

1. Read the applicable repository instructions and artifact manifest. Separate
   direct/full recovery, signed bounded segments, and signed OTA before opening
   a COM port.
2. Run `scripts/bk7258_hil_download.py profiles --board <board>` and verify the
   board identity, COM roles, transport, reset policy, and source references.
   Board and port are mandatory inputs; there is no default COM or default
   physical target.
3. For Beken BK Loader transport, read
   [references/SOP.zh-CN.md](references/SOP.zh-CN.md), then run `preflight`.
   Use `single` for one bounded/full BIN and `multi` for reviewed
   `PATH@OFFSET-LENGTH` segments. Every segment length must equal its file size,
   ranges must not overlap, and hashes are recorded. AIDK multi-segment writes
   require explicit 4 KiB-aligned `--write-bound START-LENGTH` ranges from the
   current device's inactive-slot/layout evidence, plus one SHA256 per segment.
4. Treat `artifact_kind_claim` as an operator label, not signature proof. For a
   signed package, first use the repository's package/trust verifier and only
   then pass the verified extracted BIN or manifest-declared segments. Signed
   OTA stays on the target project's existing OTA entry, not this transport.
5. Execute `run --execute --evidence-dir <new-directory>` only when still-valid
   authorization in the current conversation covers that exact board, artifact,
   port, and Flash write. Reuse explicit same-board iteration authority within
   its stated bounds; one-shot authorization remains limited to one write.
   Do not automatically retry a failed erase or write.
6. After BK Loader releases the COM port, run `debug-plan` to generate a
   board-safe command for `windows-hardware-debug`. Inspect it, then execute the
   generic tool only if capture/reset/J-Link control is in scope.
7. Report Flash transport, boot, functional behavior, and physical observation
   as separate gates.

## Current board profiles

- `aidk_ai_toy`: one 8 MiB BIN or bounded signed inactive CP/AP segments, CH340 UART0,
  loader-owned `reset reboot`, and no RTS/DTR reset. Multi-segment bounds must
  preserve the active pair/data/boot and come from current device evidence;
  RESET/CEN is the manual fallback, not a product volume key.
- `t5_board`: single or manifest-bounded multi-segment BK Loader input. UART0 is
  download plus console, and its USB-UART path supports inline RTS reset.
- `t5ai_core`: single or manifest-bounded multi-segment input. Download/reset
  and UART0 console are distinct COM ports; RTS is issued on the download/reset
  port. J-Link remains a separate guarded diagnostic path.

These names are aliases, not hard-coded COM assignments. Read
[references/board-profiles.json](references/board-profiles.json) for exact
defaults and evidence pointers. Add a new board only after its wiring, loader
form, reset mechanism, and console baud are evidenced; do not clone another
board's profile speculatively.

Keep this skill and its `scripts/` and `references/` together. The sibling
`../windows-hardware-debug/` is the existing post-download transport; if it is
unavailable, report that capture step as unavailable and retain Flash results.
Obtain loader location and artifact/layout inputs from the caller's project,
not a previous operator's installation or release directory.

## Safety and acceptance

- Profile overrides fail closed unless `--allow-profile-override` is supplied
  after reviewing the electrical and loader consequences.
- AIDK must never use COM RTS/DTR. T5 RTS capability must never be generalized
  back to AIDK.
- Do not feed `.zip` or `.bkpack` directly to BK Loader. Do not chip erase,
  write OTP/eFuse, provision keys, or change lifecycle/debug-lock state.
- `FLASH_PASS` requires all required BK Loader success markers and no failure
  marker in the new log. The loader process exit code is advisory.
- `BOOT_PASS` requires fresh target-side boot evidence. `FUNCTION_PASS` requires
  the requested hardware path to run. `PHYSICAL_PASS` requires human observation
  for screens, sound, light, motion, or other non-log-visible effects.

## Resources

- `scripts/bk7258_hil_download.py --help`: deterministic interface.
- `scripts/test_bk7258_hil_download.py`: host-only regression tests; never opens
  a COM port or debugger.
- `references/SOP.zh-CN.md`: operator commands, evidence layout, and failure
  handling.
- Sibling `windows-hardware-debug/SKILL.md`: generic UART/reset/J-Link rules.
