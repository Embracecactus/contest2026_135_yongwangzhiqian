# ADR-014: Retire BK7259/v4; use BK7236 v2.0.1 only as secureboot reference

- Status: Accepted
- Date: 2026-08-06
- Decision owner: Project owner

## Context

The project targets BK7258/T5-AI. Beken `release/v4.0.1` is a BK7259 branch
and is retired for this project. Continuing to treat it as architecture
evidence risks importing different Flash, TrustZone, AP-start and packaging
assumptions into the BK7258 port.

The public `bk_idk release/v2.0.1` tree has a BK7236 secureboot project and
matches the BK7236 BootROM image identity observed on this board. It provides
the relevant official BL1/BL2/TF-M vocabulary and is the selected read-only
reference for the secureboot integration.

## Decision

1. BK7259 and Beken `release/v4.0.1` are prohibited for source, builds,
   tests, validation and architecture decisions in this project.
2. Official BK7258 SDK v3.1.1.9 remains the only runtime SDK and static
   library input.
3. `bk_idk release/v2.0.1` at commit
   `650e754e12fe1e43c37ce2316a973668b033fd48` may be inspected read-only for
   BK7236/BK7258 BL1/BL2/TF-M secureboot integration. It must not replace the
   v3.1.1.9 runtime SDK or be modified.
4. This supersedes ADR-013's v4 baseline/reference decision.

## Consequences

- Secureboot work follows the official chain `BL1 -> BL2 (MCUboot) -> TF-M ->
  Non-Secure application`, subject to BK7258 CP/AP handoff verification.
- Existing NuttX, apps, SDK source and SDK archives remain read-only.
- No Flash, OTP/eFuse, secure-boot enable, AES-key provisioning or debug-lock
  action is authorized by this decision.
