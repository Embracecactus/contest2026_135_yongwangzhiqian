# ADR-021: Let MCUboot order slots before enforcing the CP/AP pair gate

- Status: Accepted; valid newer-B selection, generation rejection and AP-vector fallback are hardware-proven.
- Date: 2026-08-07
- Scope: board-owned BK7258 BL2 only.

## Decision

The BL2 first invokes upstream `boot_go()` with both A/B slots visible.  This
keeps NuttX-pinned MCUboot responsible for its normal image-version ordering.
The board wrapper then enforces the BK7258 launch contract: the returned CP
slot must have an AP in the same physical pair, matching `ih_ver` and matching
security-counter presence/value, and its signed AP vector must be usable.

If that candidate is not launchable, BL2 retries `boot_go()` with A and B
individually isolated.  A successful secondary candidate is remapped through
the verified A execution window before CP handoff.  The trace records the
selected slot as `B2SELA` or `B2SELB`.

## Rationale

Always trying A before B made a valid newer B image invisible to MCUboot's
version policy.  Conversely, exposing both images without a board gate allows
MCUboot's independent CP/AP image IDs to produce a cross-slot launch.  The
two-step path keeps both responsibilities explicit without changing upstream
NuttX or SDK sources.

## Verification boundary

The host build, package metadata, and marker presence are verified.  The
staged B candidate at version `19.1.1` was written only to the B pair and the
board reached `B2SELB -> B2APOK -> B2HANDOFF -> NSH`.  The original `18.1.1`
pair was restored and reached `B2SELA -> B2APOK -> B2HANDOFF -> NSH`.  This
proves the valid newer-B path.  A signed B CP `19.1.1` combined with a signed
B AP `18.1.1` then produced `B2GENBAD -> B2TRYA -> B2SELA -> B2HANDOFF -> NSH`;
the original pair was restored again.  A separately signed B AP with an even
(non-Thumb) reset vector produced `B2APTHUMB -> B2GORET -> B2TRYA` and the
known-good A pair completed `B2SELA -> B2APOK -> B2HANDOFF -> NSH`; the B
package was restored immediately afterward.  The complete board captures are
in `progress/verification/2026-08-07-mcuboot-version-aware-selection-board.md`.
