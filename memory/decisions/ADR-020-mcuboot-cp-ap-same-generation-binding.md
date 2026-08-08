# ADR-020: MCUboot CP/AP same-generation binding

## Status

Accepted and board-verified for the reversible BK7258 MCUboot experiment.

## Decision

Treat the CP and AP MCUboot images in one physical A/B slot as one launchable
generation. After upstream `boot_go()` authenticates the visible slot, the
board-owned BL2 requires:

1. equal `image_version` major, minor, revision, and build fields;
2. equal protected `IMAGE_TLV_SEC_CNT` values when a counter exists;
3. matching counter presence (both absent is the legacy-compatible case).

If the rule fails, BL2 emits `B2GENBAD`, rejects that slot, and may try the
other physical pair. The upstream NuttX MCUboot and official SDK sources stay
unchanged.

## Reason

The same-slot visibility gate prevents CP-A/AP-B mixing but does not prevent a
valid CP from being paired with a separately signed AP from another release.
The MCUboot header already carries a signed version and protected security
counter, so equality provides a small, auditable binding without inventing a
new vendor TLV or changing the image format.

The pair packer records the rule in `mcuboot_pair.json` and passes the same
version/counter arguments to both NuttX `imgtool.py` invocations. BL2 remains
the final authority because host JSON is not present at runtime.

## Evidence and boundary

A valid `18.1.1` CP plus valid `19.1.1` AP was rejected with `B2GENBAD`; the B
fallback was empty and the board reached `B2BAD` without NSH. Restoring the
matching pair reached `B2HANDOFF` and NSH. See
`progress/verification/2026-08-07-mcuboot-cp-ap-same-generation-reject-board.md`.

This is a software binding in the board-owned BL2. It does not turn the
software counter into an OTP/eFuse monotonic counter and does not claim the
unpublished BK7258 BootROM Secure Boot ABI.
