# Phase 05 — verified-baseline follow-up

> **Historical handoff (retired).** This file describes the former RV1126B overlay and is retained as AI-worklog provenance; its `board/contest_board/` source tree no longer exists. Read the [canonical RV1126B NSH port guide](../../rv1126b-nsh-port.md) and the immutable [2026-07-14 NSH baseline evidence](../../verification/2026-07-14-rv1126b-nsh-baseline.md) when interpreting it. Do not use these historical ownership paths as instructions for the current BK7258 tree.

## Scope and ownership

- **Workspace:** `$WORKSPACE` is the full OpenVela workspace; the team overlay is `$WORKSPACE/contest2026_135_yongwangzhiqian`.
- **SDK:** `$SDK` is an RV1126B SDK copy used only for candidate packaging. Do not modify SDK source or treat generated SDK files as contest source.
- **Allowed source ownership:** work only in the Team 135 overlay, especially `board/contest_board/`.
- **Forbidden ownership:** do not modify official `nuttx/`, `apps/`, `packages/`, or the generated `vendor/openvela/boards/contest2026_135_board` checkout.
- **Evidence boundary:** the old baseline was an uncommitted/untracked overlay at the recorded HEAD. A future tree or build cannot be called identical to it solely from the Git commit.

## Confirmed baseline invariants — do not change casually

The following behavior is board-verified and is protected until a separately documented candidate is flashed and tested:

- UART5 M0 on GPIO4_PA6/GPIO4_PA7 (`FUNC5`), 24 MHz, 1.5 Mbaud, 8N1.
- Raw UART IRQ 61 through INTMUX group 1, bit 29.
- IPIC initialization and the SOI/EOI order; the dispatch path carries raw IRQ 61 to the serial driver.
- Runtime RX/TX interrupt handling, serial ISR behavior, UART register setup, and TX kick.
- Entry/RAM placement and the current DCache-bypass policy.
- Classic Make as the only verified build backend.

Do **not** replace raw IRQ 61 with an external-IRQ offset, select UART4, restore idle-loop RX polling, or refactor the IPIC route as an untested cleanup. Early low-level output polling is distinct from the normal interrupt-driven serial driver.

## Current source map

- Startup and UART/IPIC implementation: `board/contest_board/chip/`
- Hardware register definitions: `board/contest_board/chip/hardware/`
- Board boot and late bring-up: `board/contest_board/src/rv1126b_boot.c` and `board/contest_board/src/rv1126b_bringup.c`
- Tested configuration: `board/contest_board/configs/nsh/defconfig`
- Board overview at the time: `board/contest_board/README.md` (retired with the former RV1126B overlay)
- Team mapping: [contest manifest](../../../contest2026_135_yongwangzhiqian.xml)

## Cleanup-candidate provenance

Before a cleanup build, record all of the following separately from the baseline:

1. Git HEAD, complete overlay diff/untracked-source state, and the exact candidate intent.
2. Make command, compiler/toolchain identity, SDK identity, and packaging directory.
3. ELF/bin/FIT/update-image paths, sizes, hashes, and any staged-copy byte-equivalence checks.
4. A statement that the result is a **build-verified candidate**, not a board-verified replacement.
5. If flashed, board revision, exact flash command, timestamped raw serial capture, and the commands actually exercised.

Do not overwrite or relabel retained baseline artifacts. Use a disposable or copied SDK output area when producing candidate images.

## Make-only rebuild procedure

```bash
export WORKSPACE=/absolute/path/to/open-vela
export SDK=/absolute/path/to/rv1126b-sdk

cd "$WORKSPACE"
# Use distclean only when intentionally preparing a new candidate.
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

Do not use CMake as a validation substitute. For objcopy, FIT packaging, and `updateimg`, follow the exact candidate procedure in the [canonical guide](../../rv1126b-nsh-port.md); the documented `mkimage` command is part of the protected packaging recipe.

## Required review and evidence checklist

### Static/documentation review

- Check that source changes stay inside the overlay and generated objects/dependency files are not treated as source.
- Confirm the UART5 M0, raw IRQ 61, INTMUX group 1 bit 29, IPIC SOI/EOI, and interrupt-driven runtime serial route remain intact.
- Confirm no current documentation revives UART4, placeholder-board, or idle-loop RX-polling guidance.
- Link new records to the immutable evidence instead of duplicating its hashes or transcript.

### Hardware evidence still missing

The current baseline already verifies boot, banner, prompt, RX, `help`, and prompt return. A follow-up board test should additionally preserve:

- `uname -a` output;
- board revision;
- exact flash command;
- timestamped raw serial capture;
- candidate provenance and artifact-copy checks.

Only after this material is captured may a newly flashed candidate be described as board-verified.

## Non-goals

This phase does not establish RPMsg/A-core communication, broader peripheral support, CMake equivalence, DCache enablement, or a production-ready BSP. Keep the scope to baseline preservation, disciplined candidate rebuilds, and evidence completion.
