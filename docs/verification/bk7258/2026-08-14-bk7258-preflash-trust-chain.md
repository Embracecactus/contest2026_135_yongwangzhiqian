# BK7258 pre-flash trust-chain verification

Date: 2026-08-14

## Scope

Add a fail-closed preflight between a signed MCUboot package and the existing
T5-Board trust roots.  This stage generated images and performed bounded
read-only J-Link inspection.  It did not start `bk_loader`, write Flash, reset
the board, alter BL1/BL2, or touch OTP/eFuse/lifecycle state.

## Implementation

- `bk7258_trust_chain.py emit` derives only public P-256 fingerprints from the
  two externally supplied development signing keys.  It resolves the actual
  BL1 and BL2 symbols and rejects a binary that does not contain those public
  values.
- BL1 and BL2 now publish their trust identities in linker-reserved fixed
  blocks (`0x0200fd00` and `0x024d2f00`).  The contract also carries exactly
  one compatibility location per identity for the boot chain already installed
  on the current board.  This prevents ordinary code growth from moving future
  probes while allowing a CP/AP-only update before a separately authorized
  boot-chain replacement.
- `pack_dual_image.py` requires the contract for MCUboot packages and forbids
  it for raw packages.  It validates the source trust bundle, stages the raw
  images and ELFs into a clean package, and validates the destination again.
  Final layout verification repeats the ELF/raw binding.
- `bk7258_auto_debug.sh` requires the verified contract for every MCUboot Flash
  action, performs only six bounded `mem32` reads (fixed plus installed-legacy
  location for each of three identities), and refuses to invoke `bk_loader`
  when J-Link fails, every compatible location is missing, or no fingerprint
  matches.  There is no normal-download bypass or root-rotation option.
- `--apps-only` constructs a CP/AP-only sparse write set so routine firmware
  validation can preserve BL1, BL2, Manifest, secondary slot and data regions.

## Host verification

- Eight focused trust tests passed: matching target, wrong BL1 root, wrong BL2
  key, missing read, read-only command set, forbidden root rotation, and clean
  package trust-bundle staging/revalidation, plus private-key error-path
  redaction.
- The existing mailbox suite remained 31/31, and BL1 policy plus PM activity
  tests passed.
- A full signed `t5_board_cp_app_mcuboot + t5_board_ap_app_mcuboot` build
  passed with MCUboot version `18.6.47` and security counter `101`; factory
  layout and RPTUN layout verification passed.
- The generated trust contract SHA-256 is
  `b2a8e35256b97a0dd4fef6da253225d770001f4f20743d44bf8e9cc5f3b97c7e`.
  CP/AP flash-segment SHA-256 values are respectively
  `63afc6de856ed18c73aae79ef2aa1758a12cca9cc49d730475cecc6b8ed5aa3a`
  and `91b0b2082557de8f8990edb4c851c1ac9fd1c1b3f966ec3b9fb68c56f2951fab`.
- An independent MCUboot repack into an empty directory passed.  Changing the
  contract's BL1 fingerprint while also repairing its outer manifest digest
  still failed with `contract fingerprint disagrees with bootloader bytes`.
  Removing the contract failed layout verification.  A raw-profile repack
  without a trust contract remained valid.
- Contract/package scans found no private material, private-key path or
  temporary key-store path.
- The real `bk7258_auto_debug.sh --flash --sparse-flash --apps-only` entry was
  exercised with the physical J-Link and a no-write loader stub.  The trust
  preflight completed before the stub, whose captured `--mainBin-multi` value
  contained only CP `@0x11000` and AP `@0x165000`; it rejected any boot, BL2,
  Manifest or filesystem argument.  Replacing J-Link with a mismatch fixture
  made the same entry exit before invoking the loader stub, with
  `writes_performed=false`.

## T5-Board read-only result

The final minimal J-Link command set was:

```text
mem32 0x0200fd40,8
mem32 0x02002774,8
mem32 0x0200fd60,8
mem32 0x02002754,8
mem32 0x024d2f00,17
mem32 0x024d27ec,17
exit
```

The fixed locations returned erased bytes because that new boot chain was not
written in this stage.  The installed legacy locations returned exactly the
required two 32-byte BL1 digests and 91-byte BL2 DER public key, so the verifier
selected the compatible addresses and reported:

```text
PASS bk7258-trust-preflight: BL1/BL2 target fingerprints match
```

Matched public fingerprints:

- BL1 X||Y SHA-256:
  `e82597007f132d2415c64ea88f0dc75bce67e574d6e9943931a0b97a849fcc02`
- BL1 SEC1 SHA-256:
  `6d17866e4d3572eff3a9b9fc1c8d33b9a1add71a13784b2feff385c38883106c`
- BL2 MCUboot SPKI SHA-256:
  `df8b7893df6afea30da3b212f26f42646404e5e7d9feae0ec2615d0c90cea26e`

Replaying the same board log against a contract with a wrong BL2 fingerprint
failed and produced `writes_performed=false`.  No Flash operation was started.

## Boundary

This closes accidental package/installed-root mismatch before download.  It
does not make the software development roots hardware-rooted, authenticate a
hostile host package, advance an anti-rollback floor, or authorize any future
BL1/BL2/OTP write.
