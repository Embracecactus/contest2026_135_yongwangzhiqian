# ADR-013: Adopt SDK v4.0.1 and reassess N17 on the official BL1/BL2 path

- Status: Superseded by ADR-014
- Date: 2026-08-06
- Decision owner: Project owner

## Context

> Historical record: the owner subsequently retired BK7259 and v4 completely.
> Do not use this ADR as authority to inspect, build, test or reference v4;
> see ADR-014 for the current rule.

The owner replaced the v3.1.1.9 SDK-baseline rule with official Beken SDK
`release/v4.0.1`. The local v4.0.1 SDK snapshot contains the official Beken
security-OTA client and packaging path: `PRIMARY_ALL`/`SECONDARY_ALL`,
Direct-XIP state, BL1 manifest generation, and MCUboot `imgtool` signing
commands. Official BK7258 security documentation identifies BL1 as BootROM
and BL2 as MCUboot.

The official Git reference `refs/heads/release/v4.0.1` resolved on 2026-08-06
to commit `d24a9fa4597129b9b7b2d972de3a84e3368de03e`. The local source snapshot
has not yet been matched to that commit, so it remains unsuitable as the
provenance source for a versioned static-library bundle.

Source inspection of that exact commit found a disqualifying BK7258 build
boundary: its only buildable SoC profiles are `bk7259` and `bk7259_ap`.
`cp/middleware/soc/` and `ap/middleware/soc/` contain no BK7258 profile, and
there is no BK7258 CP/AP role export path. The branch can therefore inform
MCUboot/security architecture research, but cannot supply an ABI-compatible
BK7258 replacement bundle.

On 2026-08-06, the official repository's complete public release set was
enumerated through GitHub's CP/AP SoC directory API. Both release branches
`release/v3.0.1` and `release/v3.1.1`, all tags from `release/v3.0.1.1`
through `release/v3.0.1.6`, and all tags from `release/v3.1.1.1` through
`release/v3.1.1.9`, contain both `bk7258` and `bk7258_ap`. All
`release/v4.0.1` tags (`.1` through `.7`) contain only `bk7259` and
`bk7259_ap`. Consequently, `release/v3.1.1.9`
(`c3b560f3b972db7bf3883edaffa3b49060a865cd`) is the newest public official
BK7258 release tag found in this repository.

The prior N17 repository-owned manifest, policy sector and format-3 journal
were designed while MCUboot was incorrectly treated as only a NuttX-hosted
option. ADR-012 records that earlier review and is superseded.

## Decision

1. SDK `release/v4.0.1` is the only active SDK baseline for new work.
2. Reassess N17 around the supported BK7258 boot chain:
   `BootROM BL1 -> BL2 MCUboot -> one signed PRIMARY_ALL/SECONDARY_ALL
   CP/AP-pair image`.
3. Treat CP and AP as one contiguous signed image initially. The official
   Direct-XIP mapping of `PRIMARY_ALL` and `SECONDARY_ALL` preserves the
   existing requirement that the two cores switch atomically.
4. Use the v4.0.1 security-OTA and packaging implementation as immutable
   reference material only. Team changes, if approved after the review, stay
   in repository-owned wrappers/adapters; official SDK and NuttX/apps remain
   read-only.

## Consequences and gates

- The previous self-managed N17 final architecture is no longer the presumed
  production target. No existing N15 deployment is invalidated or changed.
- First prove a no-OTP development boot chain and the exact v4.0.1 source /
  artifact provenance. The local SDK snapshot has packaging-side MCUboot
  commands but no tracked Git metadata and no located BL2 runtime source or
  `bl2.bin`; it is not yet sufficient to build an authoritative BL2.
- OTP/eFuse writes, secure-boot enable, flash-AES key provisioning, hardware
  rollback-counter provisioning and debug locking remain forbidden. A full
  Flash re-layout or board write still requires fresh, range-specific owner
  authority.
- This decision does not authorize importing code, modifying official trees,
  changing the board partition table, building firmware, flashing hardware,
  committing or pushing.

## MCUboot research conclusion

- The v4.0.1 Beken BL2 source uses MCUboot as a pre-OS second bootloader, not
  as an application. Its `flash_map.c` maps `PRIMARY_ALL` and `SECONDARY_ALL`
  as whole slots and programs the hardware Direct-XIP offset only after BL2
  has selected a valid image. Its secure-XIP sample places TF-M Secure, CPU0
  and AP code consecutively in each slot and signs that whole span once. This
  is the right atomic CP/AP *model*, but its platform layer is BK7259-specific.
- The public BK7258 v3.1.1.9 archive exposes MCUboot-facing runtime APIs and
  its library provenance declares internal `components/mcuboot` and
  `components/tfm` dependencies, but the archive and the public v3.1.1.9 tag
  contain neither a BK7258 TF-M/BL2 source tree nor a secure-boot project.
  An ABI-compatible BK7258 BL2 cannot be reconstructed by merely enabling a
  v3 Kconfig option or relinking existing CP/AP archives.
- NuttX `apps/boot/mcuboot` is a later NuttX-hosted bootloader application:
  it opens configured MTD character devices such as `/dev/ota0` and
  `/dev/ota1`. It is valuable as an MCUboot-core reference but cannot occupy
  the BK7258 BootROM/BL2 position or establish a hardware root of trust.
- The only source-compatible production paths are: obtain the matching
  official BK7258 TF-M/MCUboot component release from Beken, or separately
  authorize a repository-owned BK7258 BL2 port based on upstream MCUboot plus
  fully reviewed Flash/XIP/OTP/ROM work. The latter is a new boot-chain port,
  not a wrapper change, and remains outside present authority.

## Evidence

- `tools/env_tools/beken_utils/scripts/pack.py` in the v4.0.1 SDK signs
  `primary_all_code.bin` and emits a BL1 manifest for `bl2.bin`.
- `tools/env_tools/beken_utils/scripts/partition.py` builds `PRIMARY_ALL` by
  concatenating every executable `primary_*` partition after `bl2`, in CSV
  order, with required padding. It has no CPU0-only restriction: adjacent
  `primary_cp_app` and `primary_ap_app` rows can therefore form one signed
  CP/AP pair; the same rule applies to `SECONDARY_ALL`.
- `tools/env_tools/beken_utils/scripts/bl2_sign.py` invokes MCUboot imgtool
  with a 0x1000 header, slot padding and a security counter.
- `ap/components/ota/ota_security/security_ota.c` handles
  `PRIMARY_ALL`/`SECONDARY_ALL` Direct-XIP completion.
- BK7258 security documentation in the available IDK identifies BL1 as
  BootROM and BL2 as MCUboot; it remains research evidence until matching
  v4.0.1 provenance is established.
