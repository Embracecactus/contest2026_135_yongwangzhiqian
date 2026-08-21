# BK7258 AIDK AI Toy signed MCUboot package

Date: 2026-08-21

## Conclusion

`SIGNED_PACKAGE_PASS / TARGET_UNCHANGED / HARDWARE_PENDING_MANUAL_RESET`

The AIDK fixed-block profile completed a clean project BL1 -> Manifest A/B ->
project BL2 A/B -> MCUboot CP/AP A/B build.  Package structure and all public
BL1/BL2/CP/AP signatures verified.  The target was not written because the
CH340E route has UART0 TX/RX but no RTS/DTR-to-CEN reset path, and the owner was
not physically present to reset the board into the BKFIL ROM handshake.

## Authorization and inputs

- [ADR-030](../../memory/decisions/ADR-030-aidk-first-provision-project-boot-chain.md)
  authorizes replacing the AIDK factory chain with the project chain without
  matching the factory trust identity.
- Layout: `bk7258-381e2cdd1286ac59` (`fixed-block`).
- Version: `1.0.0+1`.
- MCUboot protected security counter: `1`.
- BL1 Manifest counter and compiled rollback floor: `1`.
- New development P-256 roots were generated outside Git. Private material
  and private-key paths are not recorded in the repository or this record.
- BL1 public fingerprint:
  `bedf62b3d0fde409aeddb63151a7001b2c7255b4ff75184ae6993e53a8c0de8a`.
- MCUboot public fingerprint:
  `4b1def6513130ac526c7351a64bfad5062c6affd8429002322b4584a5133b8db`.

## Host build and package evidence

- Clean `bk7258.py build --boot mcuboot`: PASS.
- BL1: 10,274 bytes.
- BL2: 10,580 bytes; padded SRAM copy size 10,592 bytes.
- CP resolved config SHA-256:
  `ceb12038758514ce85c9049a2cef2ea2d7ae0c395f6004988327f59a299d6644`.
- AP resolved config SHA-256:
  `658520f5b983232ea032edc4f0b750a20fc5611654c83a3cfea820a1a808dd22`.
- Signed package SHA-256:
  `29aa1757eba5efc63e1d2fca20c9015cab327c699540f78d24385576f23dc11e`.
- `bk7258.py verify package`: PASS, eight images, signed evidence.
- `bk7258.py verify trust`: PASS for public BL1/BL2/CP/AP signatures.
- Package extraction and the independent nine-entry package/member
  `SHA256SUMS` check passed. The retained public package is outside Git at
  `../aitoy-mcuboot-provisioning-2026-08-21/`.

## First-provisioning sparse contract

| Artifact | Physical offset | Bytes | SHA-256 |
|---|---:|---:|---|
| boot | `0x000000` | 69,462 | `40268332...5447d` |
| CP A | `0x011000` | 173,706 | `0b6b1332...65248` |
| AP A | `0x165000` | 168,572 | `fd532047...955bc` |
| CP/AP B pair | `0x286000` | 2,576,384 | `61c3f8fc...122f` |
| Manifest A | `0x50b000` | 256 | `8e95cb9c...9e45` |
| Manifest B | `0x50c000` | 256 | `3651f27c...0fad` |
| BL2 A | `0x51d000` | 11,254 | `96b91e65...91c8` |
| BL2 B | `0x53f000` | 11,254 | `96b91e65...91c8` |

The contract excludes `usr_config`, `reserved_data`, `easyflash*`, `sys_rf`
and `sys_net`. Chip erase, OTP/eFuse, debug lock, fixed-SD-NAND formatting and
USB resource-volume writes remain forbidden.

## Hardware evidence and blocker

- An attempted B-pair download connected to COM8 at 115200 for the initial
  handshake but failed `Getting Bus` and `BL2 Getting Bus`. The BKFIL log has
  no erase/write marker and ends with `Writing Flash Failed`; no target range
  was changed.
- Two complete-Flash read attempts failed at the same GetBus stage and created
  no backup image. They performed no erase or Flash write.
- The schematic explains the result: CH340E connects UART0 P10/P11 but does
  not control CEN. BKFIL therefore needs a physical RESET/CEN action or a full
  target power cycle while it is waiting.

## Exact continuation

When the owner is physically present, start BKFIL read on COM8 at 115200 and
manually reset the target during `Getting Bus`. Capture the complete 8 MiB
Flash twice, require both files to be exactly 8,388,608 bytes and byte-identical,
then execute the eight manifest-declared sparse writes with boot last. Do not
claim hardware acceptance until a post-write UART trace proves BL1, BL2,
MCUboot, CP NSH and AP READY without a fault marker.
