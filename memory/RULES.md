# Project Rules

Last reviewed: 2026-08-03

## Domain invariants

- Official NuttX/apps/SDK source and SDK static libraries are read-only deliverable inputs. Use team-owned wrappers; see [ADR-001](decisions/ADR-001-wrapper-only-official-source-boundary.md).
- CP is the sole owner of flash/LittleFS, the Beken Bluetooth Controller, AP lifecycle, and PSRAM hardware initialization.
- AP is the sole stock NuttX Bluetooth Host/GAP/GATT owner and a PSRAM consumer only.
- AP remains one native SMP cluster and one RPTUN peer; physical CPU2 is not a second peer.
- N14 upper 8 MiB PSRAM is boot-tested/reserved, not a general heap; see [ADR-002](decisions/ADR-002-n14-psram-ownership-and-layout.md).

## Permissions and ownership

- Temporary diagnostic edits to official trees are allowed only for debugging and must be removed before a checkpoint.
- Do not commit, push, open a PR, flash a destructive factory image, or mutate external repositories unless the user grants the corresponding authority.
- Preserve unrelated dirty or untracked work. Resolve exact targets before any destructive operation.

## Failure and recovery behavior

- Initialization, transport, allocator, or AP startup failures fail closed and must not be converted into READY with a warning.
- Every wait in board diagnostics and lifecycle control is bounded.
- Stale AP/RPMsg state is generation-scoped; rollback uses a verified prior profile/artifact rather than reusing stale state.

## Security and privacy rules

- Never record passwords, tokens, private keys, session cookies, or personal data in repository memory.
- Proprietary SDK archives remain ignored and must not be redistributed; only manifests/provenance metadata are versioned.
- Do not claim secure BLE, power-loss safety, cache coherency, or production SLA without a dedicated accepted stage and evidence.

## User-experience conventions

- Do not launch `BLEDebug.EXE`; the project owner reports that it makes the Windows host unusably slow. Use repository no-GUI BLE tooling.
- UART and test tools emit stable machine-readable PASS/FAIL lines with bounded timeouts.

## Engineering conventions

- At a workspace root containing `.codegraph/`, use CodeGraph before grep/file reading when locating or understanding code.
- Use `rg`/`rg --files` for text and file discovery when CodeGraph does not cover the team repository.
- Use `apply_patch` for manual file edits and preserve user-authored changes.
- Update stage docs and `progress/CURRENT.md` after material implementation, verification, commit, deployment, rollback, or blocker changes.
- Keep `memory/INDEX.md` and `progress/CURRENT.md` concise; route completed detail to milestones and verification records.
