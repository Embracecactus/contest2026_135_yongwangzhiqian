# BK7258 SDK v3.1.1.9 tool snapshot

This directory contains the unmodified Beken CRC helper required by the
BK7258 flash format. It was copied from the official SDK checkout:

```text
/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9/
tools/env_tools/bk_py_libs/bk_crc/bk_crc16.py
```

Provenance:

- SDK: `bk_avdk_smp-release-v3.1.1.9`
- Official branch used for the project: `v3.1.1.9`
- Source SHA256: `34bbb32004e79c925c5aea59d0932cae0541b6e4b32b181c2f2f8d8ff2d46b0f`
- Purpose: Beken 32-byte data plus 2-byte big-endian CRC16 stream
- CRC polynomial: `0x8005`

The source file has no license header in the supplied SDK checkout. It is
kept as a versioned provenance snapshot so builds do not require a local SDK
installation. Do not replace it with a helper from SDK v4, BK7259, or another
chip family without a new architecture decision.

The snapshot is not the MCUboot image signer. Standard MCUboot signing remains
provided by the pinned NuttX `apps/boot/mcuboot/mcuboot/scripts/imgtool.py`.
