# RV1126B HPMCU board overlay

This is Team 135's RV1126B HPMCU BSP overlay. It is a substantive, board-tested NSH port, not the original placeholder board skeleton.

## Manifest mapping and ownership

The contest manifest maps this directory as follows:

```text
board/contest_board/ -> vendor/openvela/boards/contest2026_135_board
```

Develop the BSP here, in the contest overlay. Do not edit the generated `vendor/openvela/boards/contest2026_135_board` checkout or official `nuttx/`, `apps/`, or `packages/` trees.

## Component map

| Path | Responsibility |
| --- | --- |
| `configs/nsh/defconfig` | RV1126B HPMCU NSH configuration selected by `build.sh` |
| `chip/` | Startup, clocks, memory map, IRQ/IPIC, timer, low-level UART, serial driver, and chip build integration |
| `chip/hardware/` | RV1126B CRU, GPIO, INTMUX, timer, UART, and memory-map register definitions |
| `include/` | Board-facing declarations and configuration headers |
| `src/` | Board library integration and late board bring-up |
| `scripts/` | Linker and build-support inputs |
| `Kconfig`, `CMakeLists.txt` | Top-level board integration metadata |

For the implementation-level boot and packaging flow, use the [canonical port guide](../../docs/rv1126b-hpmcu/adaptation/nsh-port.md).

## Protected UART/IPIC baseline

The following route was present in the 2026-07-14 board-tested baseline. Treat it as protected behavior; changes require a separately built and board-tested candidate.

- Console hardware is **UART5 M0** on **GPIO4_PA6/GPIO4_PA7** (`FUNC5`), using its 24 MHz source at **1.5 Mbaud, 8N1**.
- The physical UART interrupt is raw **INTMUX source 61**, routed through **group 1, bit 29**.
- IPIC initialization and its SOI/EOI sequence are part of the route.
- Software IRQ namespace: the serial driver attaches `RV1126B_IRQ_UART5`, which equals `RISCV_IRQ_EXT + 61`. The INTMUX dispatcher uses `RV1126B_INTMUX_SOURCE_TO_IRQ(source)` to convert each active source into a NuttX IRQ before calling `riscv_doirq()`. The IRQ controller enable/disable path converts back to raw sources via `RV1126B_INTMUX_IRQ_TO_SOURCE()` for register-level RMW.
- RX and TX are interrupt-driven. Preserve the serial ISR, TX priming, UART register sequences, and the route above rather than restoring an idle-loop polling workaround.

The immutable observation record is [the 2026-07-14 NSH baseline](../../docs/rv1126b-hpmcu/verification/2026-07-14-rv1126b-nsh-baseline.md). It is the authority for the observed behavior and artifact identities.

## Build policy

Build from the full OpenVela workspace, not from this overlay:

```bash
export WORKSPACE=/absolute/path/to/open-vela
cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

Classic Make is the only backend verified for this board. CMake remains unverified and non-equivalent for this port; do not use it to make a board-validation claim.

## Generated-artifact policy

The following are generated build outputs, not BSP source: dependency files, `*.o`, static libraries, ELF/bin/FIT images, and SDK update images. In particular, treat `src/.depend`, `src/Make.dep`, `src/*.o`, and `src/libboard.a` as disposable outputs while retaining the real source and `Makefile` inputs.

Never overwrite or relabel the board-tested baseline with a later build. A later Make output is a **build-verified candidate** until it is packaged, flashed, and accompanied by a new hardware transcript. Keep its provenance and hashes separate; link to the formal baseline instead of copying its artifact identifiers.

## Current validation scope

The records under [docs/verification](../../docs/rv1126b-hpmcu/verification/) directly preserve the pre-P0/P1 baseline and PROCFS validation evidence. The [post-review recovery record](../../docs/rv1126b-hpmcu/next-stage-prompts/next-stage-prompt-2026-07-16-post-review-and-pr.md) reports that the NSH prompt, `help`, `uname -a`, `ps`, and UART RX/TX were re-verified after the dc9b8ed P0/P1 fixes. However, the submitted logs and documentation do not yet contain a NSH transcript bound to dc9b8ed or a later state (including mkimage/flash output tied to a specific post-fix build hash), so the recovery record cannot serve as independent runtime evidence on its own. This is an evidence-archive limitation, not a statement that board testing did not occur.

Three 2026-07-16 verification records now exist for the P1 candidate:

- [P1 convergence build-only record](../../docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-convergence-build.md) -- confirms classic Make independently builds the current P1 working-tree candidate with exit code 0.
- [P1 AMP FIT packaging record](../../docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-amp-package.md) -- confirms the build output was packaged into `$FW/amp.img` via `mkimage` with exit code 0; records the FIT image hash and embedded payload hash.
- [P1 board runtime record](../../docs/rv1126b-hpmcu/verification/2026-07-16-rv1126b-p1-board-runtime.md) -- confirms the P1 `amp.img` was flashed via RKDevTool.exe and boots to a responsive NSH shell with `ps`, `uname -a`, and UART RX/TX all verified. GPLL warning appeared as expected.

The P1 candidate is **board-verified**: it builds, packages, flashes, and boots to NSH on real hardware.

Classic Make is the only verified build backend; CMake parity is unverified. DCache remains bypassed. RPMsg and full `update.img` flashing are outside the scope of the current board-validation results. Do not reuse any pre-fix artifact hash for a post-fix build.

## Known limitations

- **GPIO3 base address conflict.** `chip/hardware/rv1126b_memorymap.h` defines `RV1126B_GPIO3_BASE` as `0x21E00000`, while `chip/hardware/rv1126b_gpio.h` defines it as `0x21C00000`. GPIO3 is not currently used by the BSP; do not develop or enable a GPIO3 driver until the correct address is confirmed against the SDK and TRM.
