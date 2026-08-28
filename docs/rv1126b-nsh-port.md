# RV1126B HPMCU NSH port guide

> **历史赛道资料：**当前参赛主线是 BK7258。本文件只保存 2026-07 RV1126B
> 基线及其复现边界，不代表仓库当前实现或下一步计划。

This is the historical implementation guide for the Team 135 RV1126B HPMCU OpenVela/NuttX port. The authoritative record of what was observed on hardware is the immutable [2026-07-14 NSH baseline evidence](verification/2026-07-14-rv1126b-nsh-baseline.md). This guide links to that record instead of reproducing its artifact hashes or terminal transcript.

## Status and terminology

The baseline is **board-verified** for boot, NSH banner and prompt, interactive receive, `help`, and return to the prompt. It is not evidence that `uname -a` ran, nor does it establish the board revision, the exact flash command, or a timestamped raw capture.

Use these terms consistently:

| Term | Meaning |
| --- | --- |
| Board-verified baseline | The immutable 2026-07-14 image and only the behavior recorded in its evidence document |
| Build-verified candidate | A later classic-Make output that has not yet been reflashed and tested |
| Board-verified candidate | A candidate backed by a new recorded hardware test |

A cleanup build must never replace the baseline label or provenance merely because it was built successfully.

## Architecture and actual boot flow

The HPMCU image is an AMP/FIT payload staged as `rtt.bin` and packaged as `nuttx_amp.img`. Its linker entry and RAM placement are established in the baseline evidence.

The active overlay flow is:

1. `board/contest_board/chip/rv1126b_head.S` transfers from the HPMCU entry to `rv1126b_start()`.
2. `board/contest_board/chip/rv1126b_start.c` initializes the early UART first, initializes BSS and copied data, retains the current DCache-bypass policy, configures clocks, calls `boardinitialize()`, and enters `nx_start()`.
3. `boardinitialize()` is currently minimal; normal NuttX initialization after `nx_start()` establishes the runtime serial, timer, and interrupt infrastructure. The late board hook calls `rv1126b_bringup()` from `src/rv1126b_boot.c`. `configs/nsh/defconfig` enables the NuttShell configuration, so startup reaches NSH.
4. The low-level UART path provides early output; `rv1126b_serial.c` owns the normal console device and interrupt-driven receive/transmit path.

The current start policy intentionally bypasses DCache. Do not describe DCache as enabled or fixed without a new source-and-board validation record.

## RT-Thread reference and the working console route

The SDK RT-Thread SportCam BSP is a hardware reference, not the current implementation. It helped identify the live console resource selection; the OpenVela driver and startup code live entirely in the contest overlay.

| Hardware fact | RT-Thread reference value used for comparison | Current OpenVela baseline |
| --- | --- | --- |
| Console peripheral | UART5 | UART5 M0 |
| Pins | GPIO4 PA6/PA7, `FUNC5` | GPIO4_PA6/GPIO4_PA7, `FUNC5` |
| UART clock and format | 24 MHz, 1.5 Mbaud, 8N1 | 24 MHz, 1.5 Mbaud, 8N1 |
| UART delivery | INTMUX/IPIC path | raw IRQ 61 through INTMUX/IPIC |

Old research statements that select UART4 or treat a polling workaround as the normal NSH path are historical only. The [adaptation research report](rv1126b-openvela-adaptation-research.md) remains available as a decision record, but this guide is the current authority.

## IPIC root cause and repair

Early transmit and an NSH banner did not prove that keyboard input could reach the NuttX serial upper half. The failed RX path was an interrupt-delivery problem, not evidence that the console should move to UART4 or be permanently replaced with idle-loop polling.

The established repair is the complete UART5/IPIC route:

1. Keep UART5 M0 on GPIO4_PA6/GPIO4_PA7 and its 1.5 Mbaud, 8N1 programming.
2. Route UART5 raw source **61** through **INTMUX group 1, bit 29**.
3. Initialize IPIC and, for a pending source, perform its source-of-interrupt sequence before passing raw IRQ 61 to `riscv_doirq(61, regs)`; complete the matching EOI sequence afterward.
4. Attach the UART serial handler to the same raw source and use the UART RX/TX interrupt enables and ISR path.

The IPIC source loop, its CSR ordering, the raw number, the INTMUX location, serial register programming, and TX kick are protected baseline behavior. Do not refactor or deduplicate them as an untested cleanup.

## Raw IRQ semantics

`61` is the hardware UART5 source selected by the IPIC/INTMUX path. It is a **raw peripheral source number**, not a generic RISC-V machine-external cause code, Linux IRQ, GPIO number, or value to which an external-interrupt base should be added.

The distinction is deliberate: the trap/IPIC dispatcher identifies a pending source, hands the raw value `61` to the NuttX dispatcher, and the serial driver is attached to that same value. Replacing it with `RISCV_IRQ_MEXT + 61`, a generic external IRQ, or a UART4 route would change the verified route and requires independent board proof.

## Classic Make build and candidate packaging

`$WORKSPACE` must name the fully synchronized OpenVela workspace; the contest overlay is its child. `$SDK` must name a writable RV1126B SDK copy. Use a disposable SDK/output area or preserve the board-tested artifacts before packaging a candidate.

### 1. Build with the verified backend

Classic Make is the only verified backend. CMake is not equivalent for this port.

```bash
export WORKSPACE=/absolute/path/to/open-vela
export SDK=/absolute/path/to/rv1126b-sdk

cd "$WORKSPACE"
# Optional when deliberately starting a new candidate.
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

Do not claim a CMake build is a replacement for this Make-built baseline.

### 2. Produce the raw NuttX binary

```bash
"$WORKSPACE/prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-objcopy" \
  -O binary "$WORKSPACE/nuttx/nuttx" "$WORKSPACE/nuttx/nuttx.bin"
```

### 3. Stage and package the FIT image

The verified pipeline stages the binary in the RT-Thread BSP image directory. The following produces a **candidate**; it must not overwrite or relabel retained baseline files.

```bash
export SDK_BSP="$SDK/rtos/bsp/rockchip/rv1126b-mcu"
cp "$WORKSPACE/nuttx/nuttx.bin" "$SDK_BSP/Image/rtt.bin"

cd "$SDK_BSP"
$SDK/hal/tools/mkimage -f Image/amp.its -E -p 0xe00 Image/nuttx_amp.img
cp Image/nuttx_amp.img "$SDK/output/firmware/amp.img"
```

The `mkimage` invocation above is the actual FIT command used for the verified packaging flow. Do not substitute options or infer an alternative image layout without separate validation.

### 4. Create the SDK update image

```bash
$SDK/build.sh updateimg
```

Treat the SDK-generated update image as a candidate. Its output location and inputs are controlled by the SDK configuration; record the exact SDK state, candidate hashes, and copy relationships before flashing.

## Hardware test checklist

For a new candidate, capture enough information to distinguish it from the baseline:

1. Record the overlay state, build command, toolchain/SDK identity, candidate hashes, and staged-copy equivalence.
2. Flash using a recorded procedure and record the board revision and timestamp.
3. Open the console at **1.5 Mbaud, 8N1** and capture the raw boot log.
4. Confirm the NuttShell banner and `nsh>` prompt.
5. Type `help`; confirm that input is accepted, the command list appears, and the prompt returns.
6. Run and capture `uname -a` if available. It is a remaining evidence item, not a property already proven by the baseline.
7. Preserve the unedited capture with the candidate provenance, then promote its status only after review.

## Warnings and limitations

- **DCache:** deliberately bypassed in the verified baseline.
- **Build backend:** classic Make is verified; the current CMake route is non-equivalent and unverified.
- **Console output:** `up_putc()` has no bounded wait; a non-ready TX condition can block it indefinitely.
- **Layering:** some board policy remains in chip-layer code. Do not move it during protected-route cleanup without new validation.
- **Scope:** this guide verifies the HPMCU NSH/UART path only. RPMsg, A-core IPC, and broader peripheral bring-up are outside scope.
- **Historical material:** old UART4, placeholder, and polling claims are retained only in demoted historical documents and must not be used as current instructions.
