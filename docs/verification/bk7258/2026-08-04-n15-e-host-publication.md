# N15-E pending publication and reclamation verification

- Verified: 2026-08-04T05:26:43+08:00
- Scope: host model, exact-source contract, static analysis, ARM ELF and full
  dual-image builds
- SDK: official Beken v3.1.1.9 only
- Board write: **not authorized and not performed**
- Result: **host/source/ELF-verified**

## Implemented contract

N15-E adds a team-owned portable publication controller and CP wrapper. It
publishes exactly one canonical 512-byte `PENDING_B` record into the ADR-005
metadata sector only after complete live A/B pair verification.

The controller enforces all of these conditions:

- compile and runtime mutation gates are both true;
- the raw primary mapping is active;
- the CP Flash guard is acquired with a bounded timeout;
- generation, descriptor, RBL, CP/AP vectors, CRC-expanded candidate bytes,
  primary padding and all SHA-256 identities match;
- existing `PENDING_B` returns `-EALREADY`, and `CONFIRMED_B` returns
  `-EBUSY`, without mutation;
- a consumed `TRIAL_STARTED`, `ROLLBACK_A`, or structurally invalid sector may
  be reclaimed; a trusted lifecycle requires a strictly newer generation;
- the 4 KiB sector erase is read back as all `0xff` before programming;
- the record is programmed as sixteen fixed 32-byte chunks, each read back,
  followed by a complete metadata-sector parse and byte comparison;
- authority, timeout and lock state are rechecked across the operation.

An interrupted publication cannot produce a trusted mixed-generation record.
Until the final record validates, the selector remains fail-closed on A.

## Automated evidence

The then-current format-1 `verify_bk7258_ota_publish.py` passed:

- positive cases: 5;
- negative/corruption/timeout cases: 142;
- erase reset boundaries: 8;
- program/reset boundaries: 112;
- program granules: 16;
- portable core `-Werror` and GCC `-fanalyzer` checks;
- exact official v3.1.1.9 source and binary hashes;
- final Boot/CP ELF closure.

That verifier and its unused portable core were retired after the format-2
dual-bank implementation replaced them. The successor format-2 verifier was
also removed when the whole custom N15/N17 lifecycle was retired. The results
below remain historical evidence and are not a current build gate.

Both profiles completed the full build wrapper:

- normal `cp_nsh_psram + ap_smp_psram`: all mutation gates remain closed;
- validation `cp_nsh_ota + ap_smp_psram`: compile gates are present, runtime
  gates still initialize false and require a generation-bound command token.

Latest normal ELF hashes:

- Boot ELF: `df982fb84ac02b3dfdbc1c8c039f7366dd05bc56fea2e3d50a135953cda11ded`
- CP ELF: `2b93693bc4b852f55b5e0ebb93589a86ef8bd6b12455154f89cfd843655e0678`
- AP ELF: `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc`

Latest validation ELF hashes:

- Boot ELF: `78709c4df09aaa29d78447d86df6ae4c1602ca0fb38f0153547e3f0ebf263e00`
- CP ELF: `0151c254714e88be595891ad7bd500d33817507863244fe1ba529e978eb89d15`
- AP ELF: `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc`

The normal image contains no `bkota`, runtime write setter, or validation
configuration symbol. Its PSRAM report remains
`upper_8m_policy=boot-tested-unallocated`.

## Boundary

This proves publication and reclamation logic in host/source/ELF scope. It
does not prove Flash timing, wear, power-cut recovery or physical remap.

At this historical N15-E checkpoint, `CONFIRMED_B` was deliberately
non-reclaimable and inactive-A staging was not implemented. ADR-006 format 2
later superseded that limitation with dual-bank symmetric rotation; see the
format-2 verification record for the current claim.
