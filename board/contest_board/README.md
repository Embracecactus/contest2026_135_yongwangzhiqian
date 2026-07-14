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

For the implementation-level boot and packaging flow, use the [canonical port guide](../../docs/rv1126b-nsh-port.md).

## Protected UART/IPIC baseline

The following route was present in the 2026-07-14 board-tested baseline. Treat it as protected behavior; changes require a separately built and board-tested candidate.

- Console hardware is **UART5 M0** on **GPIO4_PA6/GPIO4_PA7** (`FUNC5`), using its 24 MHz source at **1.5 Mbaud, 8N1**.
- The UART source is raw **IRQ 61**, routed through **INTMUX group 1, bit 29**.
- IPIC initialization and its source-of-interrupt/EOI sequence are part of the route.
- The dispatcher passes raw IRQ 61 to the serial path; do not apply a speculative generic external-IRQ offset or renumber it.
- RX and TX are interrupt-driven. Preserve the serial ISR, TX priming, UART register sequences, and the route above rather than restoring an idle-loop polling workaround.

The immutable observation record is [the 2026-07-14 NSH baseline](../../docs/verification/2026-07-14-rv1126b-nsh-baseline.md). It is the authority for the observed behavior and artifact identities.

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

The baseline confirms boot, banner, NSH prompt, interactive RX, `help`, and return to the prompt. It does not yet confirm `uname -a`, board revision, the exact flash command, or a timestamped capture. DCache remains bypassed, and RPMsg is outside the scope of this board-validation result.
