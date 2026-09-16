# `shaniu-eye-pack-v1` binary contract

All integers are little-endian.  The pack is bounded to 32 MiB, has one
128-byte header, 2 to 64 fixed-size table entries, zero alignment padding, and
then entry payloads.  Unknown versions, flags, kinds, codecs, non-zero reserved
fields, overlaps, gaps with non-zero bytes, and trailing bytes are invalid.

## Header (128 bytes)

| Offset | Size | Field | Version 1 requirement |
|---:|---:|---|---|
| 0 | 8 | magic | `SHNEYE1` followed by NUL |
| 8 | 2 | version | `1` |
| 10 | 2 | header size | `128` |
| 12 | 2 | entry size | `64` |
| 14 | 2 | entry count | `2..64` |
| 16 | 2 | width | `160` |
| 18 | 2 | height | `160` |
| 20 | 2 | renderer API | `1` |
| 22 | 2 | flags | `0` |
| 24 | 4 | revision | non-zero, author-controlled monotonic revision |
| 28 | 4 | TOC offset | `128` |
| 32 | 4 | payload offset | four-byte aligned end of the TOC |
| 36 | 4 | total size | exact file size |
| 40 | 4 | TOC CRC32 | CRC32 of all 64-byte entries |
| 44 | 4 | payload CRC32 | CRC32 from payload offset through EOF |
| 48 | 32 | pack ID | NUL-terminated ASCII identifier, zero padded |
| 80 | 32 | source SHA-256 | canonical authoring JSON identity |
| 112 | 16 | reserved | all zero |

## TOC entry (64 bytes)

| Offset | Size | Field | Values |
|---:|---:|---|---|
| 0 | 32 | name | NUL-terminated ASCII, zero padded |
| 32 | 1 | kind | `1` palette, `2` indexed frame |
| 33 | 1 | codec | `0` raw, `1` RLE8 |
| 34 | 1 | pixel format | `1` RGB565LE, `2` INDEX8 |
| 35 | 1 | logical side | `0` shared, `1` left, `2` right |
| 36 | 2 | flags | bit 0 mirrors a shared frame for the right eye |
| 38 | 2 | width | frame width; zero for palette |
| 40 | 2 | height | frame height; zero for palette |
| 42 | 2 | palette count | palette colors; zero for a frame |
| 44 | 4 | payload offset | exact next four-byte aligned payload position |
| 48 | 4 | stored size | non-zero encoded byte count |
| 52 | 4 | decoded size | exact decoded byte count |
| 56 | 4 | decoded CRC32 | CRC32 after decoding |
| 60 | 4 | reserved | zero |

Entry zero is exactly `palette/default`: raw RGB565LE with 2 to 256 colors.
Remaining entries are sorted by name and use `expression/<id>` for a shared
frame or `expression/<id>/left` plus `expression/<id>/right` for a complete
side-specific pair.  A `neutral` expression is mandatory.

RLE8 is a sequence of `(count, palette_index)` byte pairs.  Count zero is
invalid, decoding must produce exactly `width * height` bytes, and every index
must exist in entry zero's palette.

The source SHA-256 is calculated over UTF-8 JSON re-emitted with sorted keys,
ASCII escaping, compact separators, and one final newline.  The CLI prints a
SHA-256 of the complete `.bkep` file for transport and activation manifests.
CRC and SHA identities are integrity evidence, not publisher authentication.
