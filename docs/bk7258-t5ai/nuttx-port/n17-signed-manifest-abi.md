# N17-S signed CP/AP release manifest ABI

- Status: Accepted and frozen ABI; firmware implementation and board-write gates remain closed
- Scope: recoverable N17-S software-root phase from
  [ADR-008](../../../memory/decisions/ADR-008-n17-phased-ota-authentication.md)
- Active SDK baseline: immutable official Beken v3.1.1.9
- Date: 2026-08-06
- Approval evidence: project owner accepted this 512-byte ABI on 2026-08-06

## 1. Purpose and boundary

This document defines the immutable release-authorization object that N17-S
will authenticate before either BK7258 executable slot may boot. It does not
change N15 format-2 metadata, allocate Flash sectors, implement a verifier,
enable an OTA downloader, or authorize a board write.

One manifest binds one CP/AP release pair. Slot, metadata bank, sequence,
generation, timestamp, pending/trial/confirmed/rollback state and base-version
bookkeeping remain device-local lifecycle data and are deliberately absent
from the signed bytes. The same release manifest can therefore describe the
same executable bytes in slot A or slot B.

N17-S pins a public verification key in the repository-owned Tier-1
bootloader. It does not program OTP/eFuse, enable BootROM secure boot, change
lifecycle state, disable JTAG, or claim resistance to complete-Flash and
bootloader replacement.

## 2. Canonical object

The manifest is exactly 512 bytes:

```text
0x000                                                    0x1c0       0x200
+--------------------------------------------------------+-----------+
| signed region: exactly 448 bytes                       | r || s    |
+--------------------------------------------------------+-----------+
          SHA-256 + ECDSA-P256 verification                64 bytes

SHA-256(the first 448 signed bytes) = immutable release-manifest identity
referenced by the future format-3 lifecycle journal. The signature is proof
for that identity, not part of the identity itself.
```

All multi-byte integers are unsigned little-endian. SHA-256 values are the
32 raw digest bytes in normal output order. Fixed text fields use canonical
ASCII followed by one NUL and zero padding; embedded NUL, non-ASCII, missing
NUL and non-zero bytes after the first NUL are rejected.

| Offset | Size | Field | Canonical value or rule |
|---:|---:|---|---|
| `0x000` | 8 | `domain_magic` | ASCII `BKOTA17S`; also separates this signature domain |
| `0x008` | 2 | `manifest_version` | `1` |
| `0x00a` | 2 | `manifest_size` | `512` |
| `0x00c` | 2 | `signed_size` | `448` |
| `0x00e` | 2 | `signature_size` | `64` |
| `0x010` | 4 | `flags` | `0`; unknown flags fail closed |
| `0x014` | 2 | `signature_algorithm` | `1` = ECDSA P-256 with SHA-256 |
| `0x016` | 2 | `digest_algorithm` | `1` = SHA-256 |
| `0x018` | 2 | `image_encoding` | `1` = Beken RBL with 32 data bytes + 2 CRC bytes |
| `0x01a` | 2 | `component_count` | exactly `2`, ordered CP then AP |
| `0x01c` | 4 | `key_id` | non-zero ID selecting one pinned public key; unknown IDs fail closed |
| `0x020` | 8 | `security_counter` | non-zero monotonic release authorization value; independent of version text |
| `0x028` | 16 | `product_id` | exact canonical token `openvela-bk7258` |
| `0x038` | 16 | `board_id` | exact canonical token `bk7258-t5ai` |
| `0x048` | 16 | `chip_id` | exact canonical token `bk7258` |
| `0x058` | 32 | `layout_sha256` | digest of the canonical generated partition model |
| `0x078` | 24 | `release_version` | `[A-Za-z0-9][A-Za-z0-9._+-]{0,22}` plus canonical NUL padding |
| `0x090` | 4 | `pair_physical_size` | exact raw slot span fixed by `layout_sha256` |
| `0x094` | 4 | `cp_logical_length` | non-zero CP application length within the layout capacity |
| `0x098` | 4 | `ap_logical_length` | non-zero AP application length within the layout capacity |
| `0x09c` | 4 | `reserved0` | all zero |
| `0x0a0` | 32 | `pair_sha256` | digest of the complete raw CRC-expanded slot span |
| `0x0c0` | 32 | `cp_sha256` | digest of the decoded CP application bytes only |
| `0x0e0` | 32 | `ap_sha256` | digest of the decoded AP application bytes only |
| `0x100` | 192 | `reserved_signed` | all zero in manifest version 1 |
| `0x1c0` | 32 | `signature_r` | P-256 scalar `r`, unsigned big-endian |
| `0x1e0` | 32 | `signature_s` | P-256 scalar `s`, unsigned big-endian and low-S |

For the current partition source
`board/bk7258/partitions/bk7258/auto_partitions.csv`:

- `layout_sha256` is
  `32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203`;
- `pair_physical_size` is `0x275000`;
- slot A starts at raw `0x011000` and slot B at raw `0x286000`;
- Manifest A is `0x50b000..0x50c000`, Manifest B is
  `0x50c000..0x50d000`, and the separately frozen one-way authentication
  policy sector is `0x50d000..0x50e000`.

The physical placement, format-3 journal and migration rules are frozen in
[the N17 layout/journal/migration design](n17-layout-journal-migration.md).

The `pair_sha256` covers every byte in the fixed raw slot span, including the
RBL header, CRC expansion and erased padding. The component digests separately
bind the decoded CP/AP application identities used by the existing N15
pipeline. Signing only the component digests would leave the surrounding RBL
container outside publisher authorization, so all three digests are required.

## 3. Signature and public-key encoding

The signature input is exactly:

```text
digest = SHA-256(manifest[0x000:0x1c0])
signature = ECDSA-P256-SHA256(digest, prehashed=true)
```

An implementation must hash the signed region exactly once. An API that
accepts message bytes receives the 448 bytes; an API that accepts a precomputed
digest receives the 32-byte digest with its prehashed mode. Passing the digest
to a message-hashing API would incorrectly hash it a second time.

The wire signature is fixed-width `r || s`, never ASN.1 DER. Both scalars must
satisfy `1 <= value < n`, where `n` is the P-256 group order. Version 1 also
requires `s <= floor(n / 2)`. Rejecting the high-S twin removes the trivial
`(r, n-s)` alternative encoding. It does not make ECDSA signing deterministic;
the journal therefore identifies the signed 448-byte payload, not the
signature-bearing 512-byte object.

The manifest does not carry a public key. `key_id` selects a bootloader-owned
table entry whose P-256 affine coordinates are each encoded as 32-byte
unsigned big-endian values (`x || y`). The verifier must reject an unknown ID,
an invalid point, a point not on P-256, or a key without release-signing usage.
N17-H may change how the same logical key authorization is rooted without
changing this manifest wire format.

The algorithm baseline follows the ECDSA requirements in
[NIST FIPS 186-5](https://csrc.nist.gov/pubs/fips/186-5/final) and the P-256
domain parameters in
[NIST SP 800-186](https://csrc.nist.gov/pubs/sp/800/186/final). Low-S is an
additional project canonicalization rule, not a claim that FIPS requires it.

## 4. Parser and verification order

The future Tier-1 verifier must fail closed in this order:

1. Read exactly 512 manifest bytes with bounded raw-Flash reads.
2. Validate magic, sizes, version, algorithms, flags, reserved bytes and
   canonical text before performing elliptic-curve work.
3. Require the exact product, board, chip and compiled layout identities.
4. Resolve `key_id`; validate the public point and fixed-width scalar ranges.
5. Enforce low-S and verify ECDSA over the first 448 bytes.
6. Require `security_counter` to meet the separately designed accepted floor.
7. Validate the RBL/container structure and hash the complete raw pair span.
8. Decode the CRC-expanded container and hash the exact CP/AP logical lengths.
9. Require the RBL download version to equal `release_version` and all three
   computed digests to equal the signed values.

No CRC is added to the manifest. Before signature verification, malformed or
torn data is rejected by canonical parsing; after it, ECDSA authenticates the
signed bytes. The mutable journal references
`SHA-256(manifest[0x000:0x1c0])`. This remains stable if an authorized signer
ever reissues an equivalent valid signature for the same signed payload.

## 5. Accepted independent vector

The frozen public-only vector is
[`vector.json`](../../../board/bk7258/scripts/testdata/n17_manifest_v1/vector.json).
It records, without any private key, all of these values:

- exact 448 signed bytes and their SHA-256;
- one public `x || y` key, `key_id`, low-S `r || s` signature and complete
  512-byte manifest;
- SHA-256 of the complete manifest;
- the three payload digests and lengths.

The repository verifier
[`verify_bk7258_n17_manifest_vector.py`](../../../board/bk7258/scripts/verify_bk7258_n17_manifest_vector.py)
uses Python's standard library for canonical parsing and payload generation,
then OpenSSL as the independent ECDSA implementation. On 2026-08-06 it
accepted the positive vector and rejected all 3,584 single-bit mutations of
the signed region plus 16 payload, identity, scalar, encoding, reserved-byte,
counter and domain-separation negative checks. The future Tier-1 C parser must
consume this same vector and produce the same decisions before its
implementation gate can pass.

The vector payloads are deterministic synthetic byte streams, not bootable
RBL images. Their `sha256-counter-v1` recipe makes the exact pair, CP and AP
lengths and digests independently reproducible without committing a 2.5 MiB
fixture. The vector's signed-region SHA-256 is
`5dd969feafce252c80dd244083836b46aad61f7d432789f4926442ad70ddfa1e` and
its complete-manifest SHA-256 is
`613ff894d473a053e83b93f2091c37dda2952b9539ceb219d67507f0b5d1493e`.

The vector contains only a public test key and precomputed signature. Its
ephemeral private key existed only in the one-time generator process memory
and was never serialized. A private production or test signing key must not be
committed to the repository, firmware, logs or project memory.

## 6. Decisions outside this ABI

This ABI deliberately does not decide:

- whether Tier-1 uses an official v3.1.1.9 crypto wrapper or a bounded
  repository-owned verifier;
- N17-H OTP/eFuse, BootROM secure-boot, hardware-counter or provisioning
  details.

The Manifest sectors, format-3 lifecycle record, software counter-floor
ordering and fail-closed migration are accepted in the linked design. Their
firmware implementation, Tier-1 crypto closure and every board-write gate
remain separate. Freezing this ABI does not authorize any runtime
Flash/security mutation.
