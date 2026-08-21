# ADR-033: OpenVela-native Beken projection and content-locked GCC10

- Status: Accepted
- Date: 2026-08-21
- Decision owner: Project owner

## Context

The BK7258 SDK requires Arm GNU Toolchain 10.3-2021.10, while the current
OpenVela global `prebuilts/gcc/linux-x86_64/arm-none-eabi` checkout contains
GCC13.  The public OpenVela prebuilt repository has no usable GCC10 revision.
The platform also has to follow OpenVela's vendor chip/board projection rules
without asking developers to create symlinks manually.

## Decision

- Keep team-owned sources at `chips/bk7258`, `boards/bk7258` and `prebuilt`.
  Project-relative manifest `linkfile` entries expose them as
  `vendor/beken/chips/bk7258`, `vendor/beken/boards/bk7258` and
  `vendor/beken/prebuilt`.  The exact SDK remains a direct project checkout at
  `vendor/beken/bk_avdk_smp`.
- `tools/bk7258/toolchain.json` owns the official Arm archive URL, expected
  SHA-256 and version line.  `bk7258.py toolchain install|verify` is the only
  installation and validation path; the extracted archive is ignored.
- Every target stage uses the verified GCC10 directory explicitly:
  `CMAKE_PROGRAM_PATH` for NuttX CMake, absolute `CROSSDEV` for Classic Make,
  `COMPILER_TOOLCHAIN_PATH` for the SDK and absolute `TOOLCHAIN` for BL1/BL2.
- Also project `integration/beken/vendorsetup.sh` through the manifest.  It is
  sourced by OpenVela after global prebuilts and prepends GCC10 only when the
  unified builder exports `BK7258_TOOLCHAIN_BIN`.  This is required because
  NuttX ARM CMake probes a bare `arm-none-eabi-gcc` before CMake resolves the
  compiler through `CMAKE_PROGRAM_PATH`.
- Do not modify official SDK or NuttX source to make GCC13 compile it, and do
  not add warning-suppression source patches.

## Consequences

- A fresh `repo sync` creates all vendor projections; no manual symlink step is
  part of development or release instructions.
- Host `PATH` remains available for Python, CMake, Ninja, Git and Kconfig, but
  the target compiler is independently selected and verified.
- Removing the vendor setup hook while OpenVela GCC13 is present makes the
  early NuttX compiler-version probe disagree with the later GCC10 compiler
  resolution; the resulting GCC13-only flags fail closed under GCC10.

## Reversal signals

- OpenVela publishes and pins the exact required GCC10 content at its canonical
  global prebuilt path.
- NuttX stops probing a bare compiler before resolving the selected absolute
  compiler, and every target command remains content-locked without the hook.
- The manifest-pinned Beken SDK officially moves to a different toolchain.
