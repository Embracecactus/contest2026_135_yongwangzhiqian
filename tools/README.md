# Tool index

Four tool groups live here.  They are separate because they have different
consumers, hosts and trust roles; none of them is a second package builder or
a second source of the same trust decision.

| Directory | Entry point | Consumer | Host / data needs | Status |
| --- | --- | --- | --- | --- |
| `bk7258/` | `bk7258.py` (the only tracked maintainer CLI; domains under `_lib/`) | Maintainers and operators, called directly; the workspace manifest maps it to `vendor/openvela/tools/contest2026_135_bk7258` | Linux (WSL) host; builds need the pinned toolchain, signing needs the owner's private keys, deployment needs a serial port | Current |
| `bk7258-hil-download/` | `scripts/bk7258_hil_download.py` (+ host test) | The `bk7258-hil-download` Skill: transport only, after another step produced a verified artifact | Windows host with Beken `bk_loader.exe`, a real board and the board's COM port | Current, explicit invocation |
| `windows-hardware-debug/` | `scripts/*.ps1` (with `debug_session_wsl.sh` for a WSL-side session), plus `ble-advertiser/` and `ble-gatt-client/` | The `windows-hardware-debug` Skill: generic UART, J-Link and BLE evidence capture; also the capture handoff used by `bk7258-hil-download` | Windows host with the USB-UART/J-Link hardware attached | Current, explicit invocation |
| `bkvoice/` | Six Python scripts, no single entry point | Offline voice preprocessing and training experiments (GPT-SoVITS V2Pro line); not called by the firmware build | Private recordings, an authorized local dataset manifest and a separate TensorFlow/PyTorch environment | Optional/historical evidence entry, frozen |

Ownership boundaries that follow from the table:

- The main CLI owns build, SDK, signing, packaging and release decisions; it is
  also the only place that decides whether an artifact is trusted enough to
  deploy.  Hardware transport tools consume an already verified artifact and
  do not re-derive trust.
- `tools/bk7258-hil-download/` never generates a release package and never
  signs; it checks target, range and hash, performs the bounded download, and
  hands reset-synchronized capture to `tools/windows-hardware-debug/`.
- `tools/windows-hardware-debug/` is a general Windows-side tool set.  It is
  not a product build step and it is not the owner of any board policy; board
  profiles supply the target facts.
- `tools/bkvoice/` is an independent optional workflow for private voice
  experiments.  It is unrelated to the firmware wake-word commands
  (`voice kws ...` in `tools/bk7258/`) and unrelated to the deployed
  acknowledgement assets.  It stays as a reproduction entry for the recorded
  experiments and receives no new generic features.

Each group keeps its own `README.md`, `SKILL.md` and `references/`; run the
scripts from their own directory so the relative paths next to them keep
working.  For the BK7258 build/flash/debug sequence itself, start from
`tools/bk7258/README.md` and the platform SOP it links.
