# RV1126B HPMCU OpenVela / NuttX NSH Port

This repository is Team 135's overlay for bringing OpenVela/NuttX to the Rockchip RV1126B HPMCU. The project currently has a protected, hardware-tested NSH baseline.

> **Baseline status — 2026-07-14:** the board-tested image boots to NSH, accepts interactive input, runs `help`, prints the command table, and returns to `nsh>`. The immutable record is [the RV1126B NSH baseline evidence](docs/verification/2026-07-14-rv1126b-nsh-baseline.md). It does **not** establish `uname -a`, board revision, the exact flash command, or a timestamped raw capture.

## Start here

- [Canonical RV1126B NSH port guide](docs/rv1126b-nsh-port.md) — current architecture, protected interrupt route, build/package flow, and test checklist.
- [Board overlay guide](board/contest_board/README.md) — manifest mapping, component ownership, and artifact policy.
- [Immutable board-tested baseline](docs/verification/2026-07-14-rv1126b-nsh-baseline.md) — authoritative observed result, artifact identities, and raw `help` transcript.
- [AI worklog index](docs/ai-worklog/README.md) — historical phases and the current follow-up handoff.
- [Adaptation research](docs/rv1126b-openvela-adaptation-research.md) — retained historical investigation; not current implementation guidance.

## Repository layout

The contest manifest links the team-owned overlay into a full OpenVela workspace. Make changes in this repository, not in the generated checkout targets.

| Overlay path | Workspace mapping | Purpose |
| --- | --- | --- |
| `app/hello_app/` | `packages/demos/contest2026_135_hello_app` | Native application sample |
| `quickapp/hello_quickapp/` | `packages/apps/contest2026_135_hello_quickapp` | QuickApp sample |
| `board/contest_board/` | `vendor/openvela/boards/contest2026_135_board` | RV1126B HPMCU BSP overlay |
| `docs/` | Not mapped into the build | Port records, evidence, and handoff material |
| `logs/` | Not mapped into the build | Exported AI Coding logs |

## Supported build path

`$WORKSPACE` is the **full synced OpenVela workspace** (the parent of this overlay), not the overlay directory itself. The only backend validated against the board-tested baseline is the classic Make path. CMake is not an equivalent verified backend.

```bash
export WORKSPACE=/absolute/path/to/open-vela
export SDK=/absolute/path/to/rv1126b-sdk

cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

Use the [canonical guide](docs/rv1126b-nsh-port.md) for the candidate-only objcopy, FIT, and update-image steps. Do not overwrite, relabel, or claim equivalence with the recorded baseline artifacts when producing a later build.

## Validation vocabulary

- **Board-verified baseline**: the immutable 2026-07-14 image and the behavior recorded in its evidence document.
- **Build-verified candidate**: a later Make build that has passed local static/build checks but has not been reflashed and tested on the board.
- **Board-verified candidate**: a candidate only after a new recorded hardware test.

## Current limitations

- DCache remains deliberately bypassed in the verified baseline.
- The current NSH validation is limited to boot, prompt, RX, `help`, and prompt return; `uname -a` is still unrecorded.
- UART5 M0 and its IPIC route are protected behavior; do not substitute an old UART4, polling, or placeholder-based recipe.
- RPMsg/A-core communication is outside this NSH-port baseline.

## Contribution boundary

Keep contest work in `contest2026_135_yongwangzhiqian/`. Do not modify the official `nuttx/`, `apps/`, `packages/`, or generated `vendor/` checkouts for this port. Generated objects, dependency files, libraries, and packaged images are build outputs, not BSP source; retain the source and document candidate provenance instead.
