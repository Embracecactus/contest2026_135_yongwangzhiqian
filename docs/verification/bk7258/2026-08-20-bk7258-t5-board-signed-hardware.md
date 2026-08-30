# BK7258 T5-Board signed hardware checkpoint

Date: 2026-08-20

## Conclusion

The merged six-domain BK7258 architecture completed a signed end-to-end
validation on T5-Board with the Vela-Claw AP configuration. The project-owned
BL1 handed off to the project BL2, MCUboot accepted the paired CP/AP images,
CP reached NSH, AP reported `READY`, and the Vela-Claw UI appeared.

This establishes a development-key hardware baseline. It is not production
root provisioning or hardware-enforced anti-rollback acceptance.

## Inputs

- Repository base: `ce81821f622ce20dee3af1127ac1df860f4ab021`
- Validation branch: `fix/bk7258-postmerge-validation`
- ARM prebuilt revision: `948af44a3171ff9c8645cbe4729bb21fa3e23f00`
- Compiler: `arm-none-eabi-gcc` 13.4.0
- CP profile: `board/bk7258/configs/t5_board_cp_base`
- AP profile: `board/bk7258/configs/t5_board_ap_base_vela_claw`
- Layout: `bk7258-5641c11040abf787` (`removable-block`)
- SDK tree identities:
  - CP: `a7294935c10872b7f8da883443c3e024c82269676130ee0a4e73269b6c1433f3`
  - AP: `12206ecee041f16969fe62461e21aef1c6f25ad0c14e968342ffca7470de747d`
- Image version: `1.0.0+1`
- MCUboot security counter: `1`
- BL1 Manifest counter and compiled rollback floor: `1`

Temporary development P-256 keys were created outside the repository. Their
public fingerprints were:

- BL1: `1911de0160cb2a6696c0ce4ad2a583ee981e7b2531c4e1376f3c506d6871d4d7`
- MCUboot: `86f893b0785963d49c56080651d576a3d18c96e65440ff4c139c3107544db418`

The two temporary private-key files were deleted after the final host package
verification. Public keys, packages and fingerprints were retained as
non-secret evidence.

## Host build and package evidence

The clean build completed through `bk7258.py build --boot mcuboot` using the
explicit CP/AP profiles, partition CSV, public keys, OpenSSL and rollback
floor. No compiler PATH fallback was used.

- BL1 image: 10,274 bytes
- BL2 image: 10,580 bytes; padded copy size 10,592 bytes
- BL2 raw vector: MSP `0x28040000`, reset `0x28020101`
- CP resolved config SHA-256:
  `47709f251a4a158bb47e1c5c8803d78f8335fc283fd0ecbd6e1e6c08f1efe2ca`
- AP resolved config SHA-256:
  `4603b6012be7f1ccd428e603609d4019b16a975e5d6d332d6ccab207d1b91094`

The package flashed to hardware was:

- `t5-board-vela-claw-1.0.0+1-apvectorfix.bkpack`
- SHA-256: `cbbbbad570f4df3caa43414b5c1c7d536031e0138c3dc4a8a9fc1c37702130bc`
- `bk7258.py verify package`: PASS
- `bk7258.py verify trust`: PASS for public BL1/BL2/CP/AP signatures
- Eight members: boot, CP, AP, secondary CP/AP pair, Manifest A/B and BL2 A/B

After adding a host check that binds the BL2 raw vector to its ELF, a new
host-only signed package also passed both verification commands:

- `t5-board-vela-claw-1.0.0+1-vector-guard-final.bkpack`
- SHA-256: `18010f98756dc79da6efab97c087ebfde5cfa869bd1945220ce68c0ea8a33889`

ECDSA signing is non-deterministic, so two valid packages made from the same
inputs need not have identical bytes. The later host-only package was not
flashed.

## Flash and runtime evidence

The hardware package was extracted and its eight manifest-declared segments
were written with BKFIL through COM3. The tool reported `Writing Flash OK` and
`All Finished Successfully` for each write. No chip erase, OTP/eFuse write,
lifecycle transition or debug-lock operation was performed.

Runtime evidence on COM3/UART0 at 115200 included:

- `NuttShell (NSH)` and a responsive `nsh>` prompt;
- a working interactive `help` command;
- AP boot-state magic `APBS`, state `2` (`READY`), error `0`, event `READY`;
- AP VTOR `0x02150200`, MSP `0x28050800`, clock 120 MHz;
- AP heartbeat increased from `0x138` to `0x4ed`;
- owner visual confirmation that the Vela-Claw UI appeared.

COM4 produced no firmware output. The owner initially observed continuous
backlight flicker and later confirmed that it stopped. Ten consecutive reads
of the GPIO9 control register returned `0x00000003` (output enabled and high),
so the observation is recorded as a transient display/first-frame condition,
not as a confirmed backlight GPIO defect.

## Defects exposed and fixed

- build-local MCUboot profiles lacked `Make.defs`;
- the Vela-Claw CMake source path was incorrect;
- BL1/BL2 GNU build-id notes displaced the expected raw vectors;
- linker-consumed integer macros carried C-only suffixes;
- BL2's trust key section was swallowed by a broader `.rodata` rule;
- MCUboot assertions and BL1 Manifest/rollback definitions were incomplete;
- signing emitted PUBKEY while the compiled BL2 required KEYHASH;
- CP released AP at the slot start instead of after the MCUboot header;
- signing lacked a host assertion tying BL2 raw vectors to ELF symbols.

## Open boundaries

- The development public roots are compiled into this validation firmware but
  are not anchored in OTP/eFuse or another immutable hardware root.
- The counter backend reads policy but does not provision a hardware monotonic
  counter; this is not production anti-rollback.
- Package writes were executed segment by segment rather than by a reviewed
  package-aware transport implementation.
- The Windows serial wrapper retained usable raw captures but did not emit its
  JSON summary over the UNC path.
- J-Link reset/connect was unavailable, and TF-card functional I/O was not part
  of this checkpoint.
- The host BL2 vector gate can be hardened further by checking MSP against the
  exact BL2 SRAM window. Board BL1 already performs the runtime range check.
