# Project-owned BK7258 BL1 and BL2

The official Beken bootloader is a read-only behavioral reference and is
never a link, image or package input. The executable chain is:

```text
BootROM -> project BL1 -> direct CP
                    \-> Manifest A/B -> project BL2 -> MCUboot CP/AP A/B
```

BL1 retains the reverse-engineered BK7258 cold-start, Flash, cache, watchdog
and hardened handoff behavior. BL2 retains the board-verified same-slot,
same-version and same-security-counter CP/AP gate over the pinned upstream
MCUboot bootutil sources.

Both Makefiles are internal build definitions. They have no standalone
defaults and reject missing output, toolchain, generated partition/config or
public-key inputs. `bk7258.py build` is the only maintainer entry and places
every object, ELF, map and raw binary under `out/bk7258/`; no build product is
written here.

The selected CSV owns all Flash facts. The maintained initial geometry keeps:

- one project BL1;
- Manifest A/B and BL2 A/B for BL2 update and fallback;
- primary CP/AP and the contiguous secondary `s_app` pair;
- generic persistent/reserved and immutable calibration ranges.

BL2 A/B are explicit CSV artifacts; no code infers the secondary address from
a gap. Sizes may change before compilation. Generated C/LD inputs propagate
the selected values, while checks enforce alignment, capacity and A/B
equality.

For `direct`, BL1 receives early console and SWD settings from the resolved CP
Kconfig and jumps to CP A. `image.py` applies the official 32-byte data plus
2-byte CRC encoding and builds the secondary pair.

For `mcuboot`:

1. build receives explicit BL1 and MCUboot public PEM files;
2. `trust.py` generates public-only C sources in the build tree;
3. the project BL2 links the pinned MCUboot sources and exposes its public
   anchor in `.bk7258.trust.mcuboot`;
4. BL1 exposes `.bk7258.trust.bl1` and reads the CSV Manifest pages;
5. signed package creation requires matching private keys, separate BL1 and
   MCUboot counters and one explicit image version;
6. public verification checks both BL1 Manifests, compiled BL1/BL2 anchors and
   both MCUboot CP/AP signatures.

Private keys and their paths are never embedded in firmware or package
metadata. OTP/eFuse programming, lifecycle changes and debug locking are not
reachable from these Makefiles or the normal CLI.
