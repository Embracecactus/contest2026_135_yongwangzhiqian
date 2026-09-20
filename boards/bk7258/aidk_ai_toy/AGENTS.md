# AIDK AI Toy board rules

Board-local differences only.  Repository-wide validation tiers, trust and
signing safety, publication ownership and the single CLI entry
(`tools/bk7258/bk7258.py`) are defined by the root `AGENTS.md` and the
[build/flash/debug SOP](../../../docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md);
this file adds what is specific to this physical board and does not repeat
those rules.

- Layering follows the repository contract: this directory owns electrical
  facts and board policy (pins, polarity, connectors, role configs, partition
  and release-policy selection); SoC mechanisms stay in `chips/bk7258/`.
  Scope is set per task by the owner, not fixed to this subtree.
- `openvela.conf` selects the CP/AP role configs, the partition layout
  `boards/bk7258/common/partitions/bk7258/bk7258_ab_fixed_block_full_release.csv`
  and the common release policy.  That CSV is the only Flash geometry source;
  layout CSVs are maintained centrally, never copied into a board directory.
- Signed releases use the board preset (`--board aidk_ai_toy`) and MCUboot.
  Whether a build needs `--clean` follows the root validation tiers — it is
  not a default release step and not a substitute for the dedicated
  boot/trust/layout path.
- Wired recovery is whole-device: one complete 8-MiB operator image at address
  zero, materialized from exact same-unit readback and accepted-base evidence;
  the immutable tail must stay byte-identical and nothing is copied to another
  unit.  Details: SOP "MCUboot build and signed package" and "Persistence".

## Connectors, reset and USB

- CH340 Type-C (CH340E → UART0 TX/RX): BK Loader recovery and CP console.
  RTS/CTS are not connected to CEN; never use COMx RTS/DTR as reset, and the
  host COM number is fixture state, not a board identity.
- Native Type-C (BK7258 USB0 DP/DM): signed CP/AP OTA transport only
  (chip-level `BK7258_OTA_SOURCE_USB` beside the file and HTTP sources; AP
  stages, CP is the only on-chip writer, BL2 owns trial/revert, the CP
  Supervisor confirms).  It is not raw DFU/MSC Flash; this board only selects
  the source and supplies port wiring.
- Before a BK Loader download, leave native USB MSC safely (eject it or switch
  back to CDC), then use the atomic software-reset handoff, replacing the port
  by the actual fixture port (discover it; do not hardcode COM8):

  ```text
  bk_loader.exe download -p "$PORT" -b 460800 -s 0 -i FULL_FLASH.bin \
    --swrst "reset reboot" --hard-reset 0 --reboot 1 --uart-type CH340 \
    --fast-link 1
  ```

  `--fast-link 1` is required on this board's CH340 path; without it the
  handoff timed out before erase/write.  If software reboot is unavailable,
  press and release K1 once when the loader prints `Getting Bus`.  An isolated
  relay, PhotoMOS or open-drain fixture across K1 may automate that fallback;
  do not drive CEN from RS-232-level control signals.

- If native USB does not enumerate, confirm the cable is on USB0 rather than
  the CH340 connector.  The expected AP log is
  `AIDK USB OTA: ready ep=02/82 protocol=1 max-payload=128`.
- Device-unique state stays target-bound even when only CP or AP changes;
  reprovisioning, key operations and configuration rollback follow the root
  trust rules.  The device's runtime Skill mechanism (`/data/agent/skills/`)
  is a product feature described in the technical report and skill documents,
  not part of these development rules.
