# BK7258 Tier-1 Bootloader (asm trampoline + C main + asm hardened epilogue)

This is the Tier-1 rewrite of the hand-written minimal BK7258 bootloader. It
keeps the **verified** cold-boot init sequence and jump target (app
@ logical `0x02010000`) unchanged, but restructures the binary into three
clean layers and adds three Tier-1 features:

| Feature | What | Where |
|---|---|---|
| **I** UART1 boot logging | progress + diagnostics | `boot_main.c` |
| **A** FAL partition table parse | locate `cp_app`, derive logical addr | `boot_main.c` |
| **J** Hardened jump epilogue | VTOR / dsb / isb / MSP / clear r0-r12 / bx | `start.S` |

## Files

```
board/bk7258/bootloader/
  start.S                            asm: vectors + bl magic + Reset init + bl c_main + hardened jump
  boot_main.c                        C main: FAL parse + app header check + UART logs
  boot_runtime.c                     cache/MPU, secondary-core, and handoff normalization
  research/adr003/                   superseded sector-swap prototype; never linked
  bootloader.ld                      FLASH @ 0x02000000, slot 0x10000
  Makefile                           arm-none-eabi-gcc freestanding
  bk7236_pack_min_bootloader.py      BK CRC packer (copied from $ZEPHYR_PORT/tools/)
  README.md                          this file
```

## MCUboot BL2 chain (MCUBOOT profile)

The `cp_nsh_mcuboot` / `ap_smp_mcuboot` profile changes the handoff to:

```text
BL1 -> primary Manifest + BL2 @ 0x024d0000
                  \\ on failure
                   -> secondary Manifest + BL2 @ 0x024f0000
                      -> lifecycle journal slot order in SRAM
                      -> NuttX MCUboot validates one CP/AP pair at a time
                      -> CP/AP
```

Both BL2 copies reserve 128 KiB logical (136 KiB after the official 32+2
CRC expansion). The primary uses the CSV `bl2` row at raw `0x51d000`; the
secondary uses the immediately following board-reserved span at raw
`0x53f000`, ending before LittleFS. The two 256-byte board-owned Manifest
records are stored in the boot logical tail at `0x0200ff00` (primary) and
`0x0200fe00` (secondary). These records are a recoverable development
authorization layer, not a claim about the unpublished BK7258 BootROM ABI.

There is one application boot-state owner. BL1 reads the existing N15/N17
lifecycle journal and, when required, completes the pending-to-trial append;
it does not accept or launch an application image. BL1 publishes only the
preferred slot and an explicitly permitted fallback through the checked SRAM
record at `0x2801ffd0`. BL2 consumes that record once and runs the pinned
upstream MCUboot `boot_go()` against each permitted physical CP/AP pair in
order. It never performs an independent both-slot highest-version scan, so a
valid but consumed/rejected trial cannot bypass rollback policy.

The package emits `bl2_crc.bin` and `bl2_secondary_crc.bin`, and the WSL2
download SOP writes both ranges. Existing `--manifest` invocations remain an
alias for the primary record. No NuttX or SDK source is modified and no
OTP/eFuse write is part of this path.

### Official-shaped XIP control-page artifact (opt-in)

The public BK7236/Armino XIP packer creates a separate 4 KiB
`bl1_control.bin`: it fills the page with `0xff` and copies the first 64
bytes of `bl2.bin` (the vector hand-off) to offset zero. The repository-owned
adapter reproduces that packaging rule without changing the active layout:

```bash
make bl1-control                 # reads bl2/bl2.bin
# or: make bl1-control BL2_IMAGE=/path/to/bl2.bin \
#                    BL1_CONTROL_IMAGE=/tmp/bl1_control.bin
```

This target is not part of the default `make` and must not be flashed as a
claim that BK7258 BootROM accepts the undocumented secure-boot ABI. It is a
reversible artifact for the ongoing BL1/BL2 reverse-engineering work; the
generator performs no signing, device I/O, OTP, or eFuse operation.

The same page-size rule is available for the candidate Manifest generator:

```bash
python3 make_bl1_manifest.py --format beken-candidate-v1 \
  --container-size 0x1000 --bl2 /path/to/bl2.bin \
  --private-key /tmp/bk7258-bl1-manifest-dev-key.pem --out primary_manifest.bin
```

Use the same command with the secondary BL2 XIP address and output name for
`secondary_manifest.bin`. Without `--container-size`, the existing 256-byte
boot-tail record is unchanged.

The generator checks that the private key derives to the board-owned
development root compiled into `boot_bl1_manifest_key.c`. A random EC256 key
is intentionally rejected at packaging time; otherwise BL1 would reject the
resulting page with the root-anchor error (`rc=0x4`). This root is only a
reversible software test root and is not the unpublished BK7258 BootROM/OTP
root.

## Logical / physical layout

```
FLASH logical base 0x02000000, logical slot 0x10000 (64 KiB)
  0x000..0x0FF  vector table  (64 entries: MSP=0x2809F700, Reset, NMI, HardFault, 60x default)
  0x100..0x107  bl magic      "BK7236\x10\x00"  (bytes: 42 4B 37 32 33 36 10 00)
  0x108..0x1FF  vector table  (62 entries -> Reset_Handler)
  0x200..       Reset_Handler : verbatim init -> bl c_main -> hardened epilogue
  .rodata      FAL executable partition table (3 entries x 64 B; see bl.map)

Physical image (bl_crc.bin): 32 B data + 2 B CRC16 per block -> 0x11000 bytes.
Physical slot: 0x0 .. 0x11000 on flash.
```

### FAL partition table (.rodata, `struct fal_partition`, 64 B/entry)

| name | flash_name | offset | len | logical addr |
|---|---|---|---|---|
| bootloader | beken_onchip_crc | 0x000000 | 0x010000 | 0x02000000 |
| cp_app | beken_onchip_crc | 0x010000 | 0x140000 | 0x02010000 |
| ap_app | beken_onchip_crc | 0x150000 | 0x110000 | 0x02150000 |

magic_word `0x45503130` (`'E','P','1','0'`), matches the BK SDK
`fal_partition.c` / `fal_def.h`. `c_main` scans `cp_app`; ADR-004 keeps CP and
AP contiguous so the future AB selector can remap the pair together.

## Build & pack

Requirements: `arm-none-eabi-gcc` (10.3 verified) and `python3`.

```bash
cd board/bk7258/bootloader
make            # produces bl.elf, bl.bin (logical), bl_crc.bin (physical)
make verify     # boot symbols and FAL/table inspection
make clean
```

`make verify` reports the current `bl.elf` size/map; do not reuse historical
text-size numbers after changing the clock/WDT/FAL implementation. Stable
packer invariants are:

```
packer:
  logical_size: 0x10000      physical_size: 0x11000
  sp: 0x2809f700             reset: 0x2000201   (Reset_Handler | 1, Thumb)
  magic: 424b373233361000    magic_physical_offset: 0x110
```

`bss` is 0 on purpose: `c_main` uses only `const` (`.rodata`) and stack
locals, so no C-runtime `.bss` zeroing is needed and `start.S` can call
`c_main` directly.

## Superseded N15-R2 sector-swap prototype

ADR-003 was never accepted and is superseded by ADR-004. Its exact metadata
ABI, host model, SRAM copy engine, and verifier are preserved under
`research/adr003/` as historical evidence, but no object from that directory
is linked. The accepted implementation uses the official contiguous A/B
geometry and will add only the remap/trial behavior required by later gates.

## Board flashing (bootloader @ physical 0x0)

Flash **only** the new bootloader, leaving the existing app probe untouched at
logical `0x02010000`:

```bash
# bl-only flash into the physical bootloader slot [0x0, 0x11000)
<tool> --mainBin-multi board/bk7258/bootloader/bl_crc.bin@0x0-0x11000

# normal CP + AP split update; take exact lengths from
# nuttx/bk7258-dual/bk7258-dual-image.json
<tool> --mainBin-multi \
    'board/bk7258/bootloader/bl_crc.bin@0x0-0x11000,<app_crc.bin>@0x11000-<cp_crc_length>,<app1_crc.bin>@0x165000-<ap_crc_length>'
```

`bk_loader.exe` requires the complete multi-image list as one comma-separated
argument. Passing each segment as a separate shell argument is invalid and
can make later lengths apply to the first segment.

The bootloader physical region is exactly `0x11000` bytes
(`(0x10000 / 32) * 34`); flashing outside `[0x0, 0x11000)` is wrong for the bl
image. The CP segment occupies at most raw `0x11000..0x165000`; AP occupies at
most `0x165000..0x286000`. LittleFS is raw
`0x600000..0x700000`. Normal updates remain sparse/multi-segment;
the explicit ADR-004 migration uses `all-app-factory.bin` only for raw
`0x000000..0x4fc000` plus `littlefs_factory_clear.bin` at
`0x600000..0x700000`.  The gap contains vendor `usr_config` and reserved
bytes and is deliberately absent from both files.  Neither segment may touch
the calibration tail at `0x7fa000`.
Do **not** run the commands in this repo — build/inspect only; flash on the
board with your usual BK tooling.

## Expected UART1 log

With the existing app probe (which carries its own `BK7236\x10\x00` magic at
its `0x100`) flashed at logical `0x02010000`:

```
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
BK7258 PROBE...            <- produced by the app probe after the handoff
```

If header validation fails, the bootloader prints `BAD` + a short reason
(`msp OOR` / `reset no-thumb` / `magic0` / `magic1` / `no app part`) and
hangs. UART1 is the same console/GPIO0 TXD + GPIO1 RXD (with GPIO10/11 boot
UART state preserved) used by the minimal bootloader.

## Rollback

If the new bootloader does not start, re-flash the previously verified minimal
bootloader image (no C layer):

```bash
<tool> --mainBin-multi \
    /home/lijian/project/TuyaOpen/zephyr-bk7258-port/out/custom_bootloader/bk7236_min_bl_crc.bin@0x0-0x11000
```

## Design notes / deviations from the spec

- **Init sequence is verbatim.** Every register/value in `Reset_Handler` before
  `bl c_main` is copied unchanged from the known-good `bk7236_min_bl.S`
  (cpsid i, SWD, AON/APB WDT feed, GPIO0/1 + GPIO10/11 UART1 pinmux, UART1
  clock + config). Do not edit those constants.
- **Jump target unchanged.** Still app @ logical `0x02010000`.
- **Cache/MPU cleanup is explicit.** `boot_runtime.c` clean-room reconstructs
  the Armv8-M SCB/MPU sequence from the official v3.1.1.9 normal bootloader:
  reset invalidates stale cache state, and handoff cleans/disables D-cache,
  disables/clears MPU regions, and invalidates I-cache before changing the
  application execution context.
- **UART TX poll is bounded.** Bit 20 of UART1 status (`0x45830018`) is polled
  as "TX-FIFO-not-full" per the spec, but the busy-wait is bounded so that an
  inverted bit polarity degrades to the same write-through behavior the
  verified minimal bootloader used instead of hanging the boot.
- **Hardened epilogue** preserves `r2` (app `Reset_Handler`) while clearing
  `r0,r1,r3..r12`, then `bx r2` — mirrors BK §2.7's clear-and-branch.
