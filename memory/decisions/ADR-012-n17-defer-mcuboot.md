# ADR-012: Defer NuttX MCUboot adoption and keep repository-owned OTA

- Status: Superseded by ADR-013 on 2026-08-06; retained as historical review evidence
- Date: 2026-08-06
- Decision owner: Project owner
- Approval evidence: owner accepted the MCUboot feasibility rebuttal together
  with the review corrections recorded below

## Context

During the N17 OTA signing/anti-rollback review the question arose whether the
project should adopt NuttX's MCUboot-based OTA instead of the repository-owned
format-3 A/B OTA already frozen by ADR-004/005/006/008/009/010/011.

Fact base, verified in this NuttX/apps revision:

- This NuttX tree has no `nuttx/include/nuttx/ota/ota.h` and no `/dev/ota0`
  device framework. Its MCUboot integration is
  `apps/examples/mcuboot/update_agent/mcuboot_agent_main.c`, which calls the
  upstream bootutil library directly (`#include <bootutil/bootutil_public.h>`).
- ADR-001 keeps official NuttX/apps/SDK sources read-only.

Four blockers prevent treating NuttX MCUboot as a configuration switch:

1. Boot position. A NuttX-hosted MCUboot bootloader is itself a (minimal)
   NuttX application over MTD/BCH/FTL. The level-1 bootloader runs before
   NuttX, is a 64 KiB bare-metal program, and jumps to CP from the BK
   ROM-compatible image format.
2. Image encoding. BK7258 images must carry the BK7236 header and the
   "32 bytes data + 2 bytes CRC" physical Flash encoding. The MTD exposes that
   physical byte stream, so a plain MCUboot contiguous image (standard header /
   TLV / trailer) cannot be consumed without a decode/CRC-mapping layer.
3. Dual-image unit. The switching unit is the CP+AP signed pair through BK
   hardware remap, not a single application image. MCUboot supports multi-image
   upstream, but the NuttX port has no BK7258 pair sign/confirm/rollback/remap
   adaptation.
4. Budget and hardware init. The level-1 bootloader has a 64 KiB logical budget
   with about 20 KiB already used; fitting a full NuttX bootloader plus BCH/FTL
   plus MCUboot is unverified, and BK clock, Flash, AP startup and ROM-format
   compatibility handling must be retained.

## Drivers

- Keep the official-source read-only boundary (ADR-001).
- Keep the N15 board-verified OTA working while N17 signing/anti-rollback
  proceeds.
- Avoid spending project effort reimplementing what provides no board-measurable
  benefit at this stage.

## Options considered

1. Directly enable the NuttX MCUboot bootloader: rejected. It would replace the
   working bare-metal boot chain and violates the budget and image-encoding
   constraints.
2. Adopt the MCUboot core as a library inside the bare-metal bootloader:
   viable in principle, but the effort approximates reimplementing the current
   OTA boot logic, plus a BK7258 flash-map, CRC logic mapping, and CP/AP pair
   adaptation. Not triggered today.
3. Continue the repository-owned format-3 A/B OTA and record the decision:
   accepted now; option 2 remains a future migration gated below.

## Historical decision

Continue the repository-owned bare-metal bootloader and the format-3 A/B OTA
(ADR-004/005/006/008/009/010/011). Do not treat NuttX MCUboot as a config
switch. Record MCUboot-core adoption as a future option activated only by the
reversal signals below.

Fact corrections recorded from the review:

- `/dev/ota0` is not present in this NuttX revision; the update agent calls
  bootutil directly.
- Blocker 2 is a coupling/decode problem, not "cannot be a contiguous image":
  MCUboot swap at the MTD layer copies raw sectors and remains possible; the
  blocker is the standard MCUboot image format and verification on top of the
  BK physical encoding.

## Consequences

- No NuttX/apps/SDK source changes; the ADR-001 boundary holds.
- The current OTA and N17 signing/anti-rollback remain self-implemented; they
  do not inherit MCUboot's crypto-reviewed bootutil and imgtool ecosystem.
- Future adoption of the MCUboot core requires a dedicated migration and board
  re-verification; its cost approximates reimplementing the current OTA boot
  logic.

## Evidence and validation

- Verified in this tree: no `nuttx/ota/ota.h`; the MCUboot update agent uses
  `bootutil_public.h`.
- N15 OTA is board-verified (see `progress/verification/`); N17 architecture is
  frozen by ADR-008..011 with host-model verification passing.
- This ADR grants no build, Flash mutation, board action, OTP/eFuse operation
  or JTAG change.

## Reversal signals (trigger conditions for MCUboot-core migration)

1. N17 self-implemented signature/anti-rollback fails a security review, or
   product requirements mandate a standards-based signed image toolchain (for
   example imgtool or external-image interoperability).
2. The 64 KiB boot budget is relaxed, or a smaller-footprint MCUboot core
   (bootutil only) is demonstrated to fit together with BK clock, Flash and
   ROM-compat handling.
3. A concrete need to interoperate with MCUboot-format images or ecosystem
   tools emerges.

If triggered: adopt bare-metal bootloader + MCUboot core as a library + BK7258
flash-map / CRC logic mapping / CP+AP pair adaptation, without changing
NuttX/SDK sources.

## Open questions

- The security-trust delta between the self-implemented SHA2/TLV signing and
  bootutil + mbedTLS is not yet quantified; it should be quantified by a review
if reversal signal 1 is evaluated.

## Supersession note

Historical ADR-013 temporarily adopted Beken `release/v4.0.1`; the owner has
now retired BK7259/v4 completely. ADR-014 supersedes that decision and permits
only read-only BK7236 `bk_idk release/v2.0.1` secureboot reference work. This
note grants no source import, build, Flash write, OTP/eFuse action or hardware
test.
