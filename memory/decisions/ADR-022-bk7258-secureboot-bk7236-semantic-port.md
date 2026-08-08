# ADR-022: Port BK7236 Secure-Boot Semantics to the BK7258 BL1/BL2 Chain

Date: 2026-08-07  
Status: accepted

## Context

The official BK7258 v3.1.1.9 runtime SDK does not provide a buildable Secure
Boot adaptation. Technical support confirmed that BK7236 and BK7258 use the
same security architecture, while BK7236 is single-core. The BK7236 security
documentation therefore supplies the required boot semantics, but it does not
prove BK7258 Manifest bytes, register addresses, OTP layout, or CP/AP image
mapping.

The BK7236 reference flow is:

```text
immutable BL1 -> fixed Manifest + root hash/version checks -> BL2 MCUboot
             -> signed image/TLV checks -> application handoff
```

It also documents the Beken-specific AES/CRC/padding/merge/signing order.

## Decision

1. Use the BK7236 `bk_idk` Secure Boot documentation, source and host tools as
   a read-only semantic and packaging reference.
2. Do not copy BK7236 single-core addresses, register offsets, Manifest ABI,
   OTP/eFuse fields, or TFM image mapping into BK7258 without independent
   source or board evidence.
3. Keep the BK7258 product topology as CP CPU0 plus AP SMP CPU1/CPU2. The
   board-owned BL1 authorizes a BL2 candidate; NuttX MCUboot remains the BL2
   implementation; a board-owned gate keeps CP/AP images in one physical slot.
4. Keep official NuttX and SDK v3.1.1.9 sources and libraries immutable. Any
   BK7258 adaptation remains in repository-owned wrappers, bootloader code and
   packaging code.
5. Keep development recoverable: no OTP/eFuse programming, secure-boot
   enable, lifecycle transition, hardware counter provisioning or debug lock.
   The development root may use a software fallback only while the read-only
   OTP shadow is empty; this is not a hardware-rooted Secure Boot claim.
6. Pause network OTA transport and business features until the BL1/BL2 chain,
   BK7258-specific packaging and A/B recovery are complete.

## Implementation phases

```text
SB0 scope/evidence freeze
  -> SB1 BK7236 reference extraction
  -> SB2 BK7258 read-only hardware boundary
  -> SB3 BL1 Manifest/root/version/handoff
  -> SB4 NuttX MCUboot BL2 CP/AP same-slot gate
  -> SB5 CRC/AES/merge-aware BK7258 packaging
  -> SB6 A/B recovery and minimum board matrix
  -> SB-H optional hardware-root binding (separately authorized)
```

## Consequences

- The current `B1PAGE -> BL2RAM -> MCUboot -> NSH` result is a valid
  board-owned compatibility proof, not proof that BK7258 BootROM accepts the
  candidate Manifest.
- Exact BK7258 BootROM/OTP acceptance remains an explicit open item. It can be
  closed only by a BK7258 artifact or an authorized hardware-root experiment.
- The v3.1.1.9 source order must be represented in the host reference packer:
  logical CP/AP merge, MCUboot sign/pad, optional AES, 32+2 CRC and physical
  tail/status placement. CP/AP dual-core layout may still require a
  BK7258-specific pair container rather than blindly flashing the reference
  stream; the reference is not BootROM acceptance evidence.
- Existing N15/N16 runtime functionality is preserved, but new OTA transport
  work is intentionally deferred until SB6 exits.

## Reversal signals

- Replace the candidate parser if an official BK7258 Manifest/BootROM contract
  is obtained.
- Stop and isolate the affected phase if a BK7258 board test contradicts the
  read-only hardware map or the BK7236 semantic flow.
