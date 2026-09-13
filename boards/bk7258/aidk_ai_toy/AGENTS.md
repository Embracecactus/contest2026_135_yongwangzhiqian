# AIDK AI Toy board handoff rules

This subtree is the complete ownership boundary for AIDK AI Toy adaptation.

- Modify only `boards/bk7258/aidk_ai_toy/` unless the owner explicitly expands
  scope.  Do not edit `chips/`, the Beken SDK bundle/source, NuttX, `tools/`,
  applications, or the shared BK7258 board.
- Use `tools/bk7258/bk7258.py` as the only build/package/release/verify entry.
  Do not add another packer, layout parser, signer, or raw-Flash policy.
- Use the board preset (`--board aidk_ai_toy`) and MCUboot for every release.
  A release build is always clean.  The board CSV is the only Flash geometry
  source.
- A full download does not authorize key generation, rotation or destruction.
  Follow the repository's trust rules and reuse compatible approved identities
  and artifacts. Reprovisioning requires explicit identity-operation authority;
  ordinary download acceptance or failure does not authorize key destruction.
- OTA is different from full provisioning: it must use the MCUboot root already
  installed on that device and a strictly higher generation.  Store the signer
  only in the operator's approved vault/HSM or external secret manager; this
  repository does not provide a board-private key broker.  Never log key paths,
  store secrets in Git, or copy secrets into project memory.
- For final delivery, do one complete package verification and one end-to-end
  hardware acceptance. Debug iterations use the repository's hardware-fast gates.
  Do not replace acceptance with repeated partial read/probe/download loops.
- A wired recovery uses one complete 8-MiB operator image at address zero,
  materialized from an exact full readback and accepted-base evidence for that
  same unit.  Do not use chip
  erase or copy the image to another unit; the tail
  `[0x7fa000,0x800000)` must remain byte-identical to the accepted base.
- The fitted CH340E exposes UART0 but its RTS/CTS pins are not connected to
  CEN.  Never use COM8 RTS/DTR as reset.  For a running diagnostic image,
  leave native USB MSC safely (eject it or switch it back to CDC), then let
  BK Loader atomically send the firmware `reset reboot` command with
  `--swrst "reset reboot" --hard-reset 0 --fast-link 1`; the loader must
  already be waiting for the reboot window.  If software reboot is
  unavailable, start BK Loader and press K1 RESET once when `Getting Bus`
  appears.  Holding a BOOT key is not required.
- The second Type-C port is native BK7258 USB0 Device.  It carries only signed
  OTA object reads into the unified OTA Manager; it is not raw DFU/MSC Flash.
  That transport is the chip-level `BK7258_OTA_SOURCE_USB` source, one OTA
  source beside the file and HTTP sources, so this board only selects it and
  supplies the port wiring; do not re-implement the wire protocol here.
  AP stages the pair, CP is the only on-chip writer, BL2 owns trial/revert, and
  the CP Supervisor policy owns automatic confirmation.

Normal commands and acceptance criteria are in `AUTOMATION.md`.  Start there;
do not infer an older workflow from historical logs or research notes.
