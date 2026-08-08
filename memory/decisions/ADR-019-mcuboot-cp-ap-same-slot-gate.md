# ADR-019: MCUboot CP/AP same-slot pair gate

## Status

Accepted and board-verified for the reversible MCUboot BL2 experiment.

## Decision

Keep CP and AP as two MCUboot image IDs, but never expose both physical A/B
slots to one `boot_go()` attempt. The board flash-map adapter returns an erased
view for the hidden slot. BL2 attempts primary visibility first, then secondary
visibility, and accepts the result only when the returned CP offset identifies
the visible slot. It checks the AP vector from that same slot again before the
final CP handoff.

## Reason

Upstream MCUboot validates multi-image IDs independently and `boot_rsp` reports
the first enabled image. `MCUBOOT_IMAGE_NUMBER=2` alone therefore does not
prove CP/AP same-slot atomicity. A composite signed image is deferred because
the current BK7258 CP/AP experiment must preserve the existing split images.

## Evidence and boundary

A temporary state with valid primary CP and valid secondary AP, but invalid
primary AP and invalid secondary CP, produced `B2TRYB -> B2BRET -> B2BAD` and
never reached NSH. Restoring the known-good AP A and B pair returned
`B2GOOK -> B2APOK -> B2HANDOFF -> NuttShell`.

Release-generation equality is specified separately in ADR-020. This gate and
that binding are both board-owned checks; neither is a vendor BootROM or
Secure Boot ABI and neither enables OTP/eFuse state.
