# Current Progress

Last updated: 2026-08-14
Updated by: Codex

## Active objective

The merged baseline is `4f88dc6` on `dev-ai-contest-2026`.  The working branch
is `fix/bk7258-mic-board-topology`.  It contains the uncommitted MIC
board-topology, lifecycle and audio-PM closure plus the completed pre-flash
trust-chain gate.  The final CP/AP-only image is running on T5-Board; nothing
from this working tree has been committed or pushed.

## Implemented

- MCUboot builds emit a strict public-only `bk7258-trust-chain.json` derived
  from the actual external signing keys and rebound to the packaged BL1/BL2
  ELF symbols and raw bytes.  Private-key material and paths are excluded.
- Future BL1/BL2 images publish identities in linker-reserved fixed blocks;
  the contract retains the current installed boot chain's single legacy probe
  per identity until a separately authorized boot-chain replacement occurs.
- MCUboot packaging and final factory-layout verification require that
  contract; raw packaging forbids it.  Packaging validates and stages the
  source bundle into a clean output, then revalidates the destination.  A
  detached/tampered contract fails before a downloadable package is accepted.
- Every MCUboot Flash action in `bk7258_auto_debug.sh` performs non-halting
  J-Link reads of the existing BL1 Manifest and BL2 MCUboot fingerprints before
  `bk_loader`.  J-Link failure or failure to match any permitted location for
  an identity refuses download; normal download cannot bypass the check or
  rotate roots.
- `--flash --sparse-flash --apps-only` produces a CP/AP-only write set and
  preserves BL1, BL2, Manifest, secondary slot, LittleFS, `usr_config` and the
  calibration tail.
- One shared NuttX MIC lower-half now consumes physical-board topology:
  T5AI-Core exposes MICP1/MICN1 as mono, while T5-Board exposes
  MICP1/MICN1 plus MICP2/MICN2 as stereo.  Electrical topology remains in the
  board header; product defaults for rate, gain and buffering are Kconfig;
  supported runtime format negotiation remains the NuttX audio API's job.
- MIC teardown now quiesces capture work before pause/stop/release/shutdown and
  closes partially owned DMA/audio resources symmetrically, so a completed
  close can be followed by a clean reopen.
- The immutable AP audio SDK's separate AUDIO-domain and clock calls are both
  routed to one CP-owned composite audio resource.  Its first acquire enables
  SDK module `122` then clock `30`; its final release disables them in reverse
  order, propagating errors instead of committing a false software state.

## Verification

- Eight trust-gate unit tests passed, as did the existing 31-case mailbox suite,
  BL1 policy and PM activity tests.
- Full signed `t5_board_cp_app_mcuboot + t5_board_ap_app_mcuboot` build passed
  with MCUboot `18.6.52`, security counter `106`; factory and RPTUN layout
  gates passed.  The final AP ELF retains both PM wrappers and contains no MIC
  lifecycle-validator or temporary hardware-diagnostic symbol.
- Independent positive MCUboot and raw repacks passed.  Missing contracts,
  contract/bootloader fingerprint detachment and wrong target fingerprints
  failed closed.
- The production auto-debug entry was run end-to-end with real read-only
  J-Link plus a no-write loader stub.  Its apps-only argument contained exactly
  CP/AP; a mismatch run exited before the loader stub was invoked.
- T5-Board non-halting P0/P1 SWD reads found erased future fixed blocks, then
  matched both BL1 public digests and the BL2 MCUboot SPKI fingerprint at the
  installed compatibility locations.  Each BL2 read was restricted to the
  exact 91-byte DER range.  No reset, Flash write or loader invocation occurred.
- `bash -n`, Python byte-compilation and `git diff --check` pass.
- A temporary public-API validator completed 10 T5-Board cycles of
  open/reserve/configure/allocate/enqueue/start/capture/pause/resume/stop/
  release/free/close/reopen.  It captured 12,800 stereo frames; both channels
  had non-zero energy and 12,089 frames (94.45%) differed, proving live,
  non-mirrored L/R inputs.  The validator is disabled in the final profile.
- The final `18.6.52` CP/AP pair passed the real preflight and was written only
  at raw `0x11000` and `0x165000`.  After the existing BL2 release handshake,
  AP reported `READY`, RPTUN reported `CONNECTED`, both errors were zero, and
  CP/AP heartbeats advanced across two non-halting J-Link reads.

Canonical details and public fingerprints are in the
[pre-flash trust-chain verification record](verification/2026-08-14-bk7258-preflash-trust-chain.md).

## Remaining boundary

- T5-Board dual-MIC capture and lifecycle are physically accepted.  The
  T5AI-Core mono topology remains a board-specific runtime check when that
  physical board is next available; do not infer its analog wiring from the
  T5-Board result.
- The lifecycle validator remains available only as an opt-in bounded board
  diagnostic.  Runnable delivery profiles must keep it disabled; application
  recording tests should use `/dev/audio/pcm0c` through the normal upper half.
- The roots remain recoverable software development roots.  BootROM trust,
  OTP/eFuse provisioning and hardware anti-rollback remain unimplemented and
  separately authorized.
- Broad checkpatch cleanup and unrelated untracked generated/log directories
  are not part of this change.

## Next action

Review the combined branch diff and separate intended MIC plus trust-chain
files from unrelated generated/untracked material.  Commit/push only after the
owner explicitly requests publication.  The MIC lower-half is now ready for
an upper-layer recording application on T5-Board.

## Fixed constraints

- Official NuttX/apps and Beken SDK source remain read-only.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Do not add one-off verification frameworks when the existing build/debug
  path and a focused unit test cover the behavior.
- Do not commit, push, perform a destructive/factory Flash write, or touch
  OTP/eFuse/lifecycle/debug locks without corresponding owner authority.
- GPT-5.6-Luna delegation remains temporarily disabled until the owner
  re-enables it; implementation and review stay with the primary model.
