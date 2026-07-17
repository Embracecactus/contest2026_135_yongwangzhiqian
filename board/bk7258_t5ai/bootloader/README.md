# BK7258 Tier-1 Bootloader (asm trampoline + C main + asm hardened epilogue)

This is the Tier-1 rewrite of the hand-written minimal BK7258 bootloader. It
keeps the **verified** cold-boot init sequence and jump target (app
@ logical `0x02010000`) unchanged, but restructures the binary into three
clean layers and adds three Tier-1 features:

| Feature | What | Where |
|---|---|---|
| **I** UART1 boot logging | progress + diagnostics | `boot_main.c` |
| **A** FAL partition table parse | locate `app`, derive logical addr | `boot_main.c` |
| **J** Hardened jump epilogue | VTOR / dsb / isb / MSP / clear r0-r12 / bx | `start.S` |

## Files

```
board/bk7258_t5ai/bootloader/
  start.S                            asm: vectors + bl magic + Reset init + bl c_main + hardened jump
  boot_main.c                        C main: FAL parse + app header check + UART logs
  bootloader.ld                      FLASH @ 0x02000000, slot 0x10000
  Makefile                           arm-none-eabi-gcc freestanding
  bk7236_pack_min_bootloader.py      BK CRC packer (copied from $ZEPHYR_PORT/tools/)
  README.md                          this file
```

## Logical / physical layout

```
FLASH logical base 0x02000000, logical slot 0x10000 (64 KiB)
  0x000..0x0FF  vector table  (64 entries: MSP=0x2809F700, Reset, NMI, HardFault, 60x default)
  0x100..0x107  bl magic      "BK7236\x10\x00"  (bytes: 42 4B 37 32 33 36 10 00)
  0x108..0x1FF  vector table  (62 entries -> Reset_Handler)
  0x200..       Reset_Handler : verbatim init -> bl c_main -> hardened epilogue
  0x4b8..       .rodata       : FAL partition table (2 entries x 64 B)

Physical image (bl_crc.bin): 32 B data + 2 B CRC16 per block -> 0x11000 bytes.
Physical slot: 0x0 .. 0x11000 on flash.
```

### FAL partition table (.rodata, `struct fal_partition`, 64 B/entry)

| name | flash_name | offset | len | logical addr |
|---|---|---|---|---|
| bootloader | beken_onchip_crc | 0x00000 | 0x10000 | 0x02000000 |
| app | beken_onchip_crc | 0x10000 | 0x10000 | 0x02010000 |

magic_word `0x45503130` (`'E','P','1','0'`), matches the BK SDK
`fal_partition.c` / `fal_def.h`. `c_main` scans the table by name, so moving
it to a real flash partition later needs no code change here.

## Build & pack

Requirements: `arm-none-eabi-gcc` (10.3 verified) and `python3`.

```bash
cd board/bk7258_t5ai/bootloader
make            # produces bl.elf, bl.bin (logical), bl_crc.bin (physical)
make verify     # nm + .rodata objdump sanity
make clean
```

Build output (verified):
```
text  data  bss
1353     0    0    bl.elf

packer:
  logical_size: 0x10000      physical_size: 0x11000
  sp: 0x2809f700             reset: 0x2000201   (Reset_Handler | 1, Thumb)
  magic: 424b373233361000    magic_physical_offset: 0x110
```

`bss` is 0 on purpose: `c_main` uses only `const` (`.rodata`) and stack
locals, so no C-runtime `.bss` zeroing is needed and `start.S` can call
`c_main` directly.

## Board flashing (bootloader @ physical 0x0)

Flash **only** the new bootloader, leaving the existing app probe untouched at
logical `0x02010000`:

```bash
# bl-only flash into the physical bootloader slot [0x0, 0x11000)
<tool> --mainBin-multi board/bk7258_t5ai/bootloader/bl_crc.bin@0x0-0x11000

# optional bl + app combo (replace <app_crc.bin> with the CRC-packed app image)
<tool> --mainBin-multi \
    board/bk7258_t5ai/bootloader/bl_crc.bin@0x0-0x11000 \
    <app_crc.bin>@0x11000-0x22000
```

The bootloader physical region is exactly `0x11000` bytes
(`(0x10000 / 32) * 34`); flashing outside `[0x0, 0x11000)` is wrong for the bl
image. Do **not** run the commands in this repo — build/inspect only; flash on
the board with your usual BK tooling.

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
- **`flash_cache_disable` skipped.** The BK private cache-control block
  (reverse-engineered doc §2.9, base `0xED00E000`, offsets
  `0x80`/`0x84`/`0x274`) is not confirmed against the BK7258 register map, and
  the verified minimal bootloader never touches cache. Cache disable is an
  optimization (avoids stale fetch after VTOR rebase), not a correctness
  requirement on a clean cold-boot handoff; the `dsb`/`isb` barriers in the
  epilogue already serialize VTOR + MSP writes before the branch. See the
  comment block in `start.S`.
- **UART TX poll is bounded.** Bit 20 of UART1 status (`0x45830018`) is polled
  as "TX-FIFO-not-full" per the spec, but the busy-wait is bounded so that an
  inverted bit polarity degrades to the same write-through behavior the
  verified minimal bootloader used instead of hanging the boot.
- **Hardened epilogue** preserves `r2` (app `Reset_Handler`) while clearing
  `r0,r1,r3..r12`, then `bx r2` — mirrors BK §2.7's clear-and-branch.
