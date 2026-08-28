# BK7258 Tri-Core openvela Port

English | [简体中文](README.md)

## 1. Project overview

This project provides a complete openvela/NuttX platform port for the Beken
BK7258, a tri-core Arm Cortex-M33 SoC. The same SoC implementation is shared by
the T5-Board, T5AI-Core, and AIDK AI Toy physical boards. Unlike the single-image
model used by the generic porting template, the product is a paired system: CP
NuttX runs on CPU0, while AP SMP NuttX runs on CPU1 and CPU2.

The main deliverables are:

- CP/AP/CPU2 startup, an 80-slot vector table, the SDK IRQ bridge, UART/NSH,
  timer, heap, and board bring-up;
- SDK wrappers for RPMsg/RPTUN, Wi-Fi/Bluetooth, PSRAM, multimedia, and common
  peripherals;
- a project-owned BL1, NuttX MCUboot BL2, same-slot signed CP/AP images, and
  rollback-counter enforcement;
- unified CMake build, partition generation, packaging, verification, and host
  regression entry points; and
- traceable source, build, hardware-console, and AI Coding evidence.

A result applies only to the configuration and physical board named by its
evidence record. See [progress/CURRENT.md](progress/CURRENT.md) for dynamic
status, the [porting report](docs/platforms/bk7258/porting-report.md) for technical
details, and the [official compliance review](docs/platforms/bk7258/official-compliance-review.en.md)
for the item-by-item interpretation of the official checklist.

## 2. Competition track

**New hardware porting.** The work integrates the BK7258 tri-core startup,
chip drivers, board configurations, Beken SDK, and secure boot chain into
openvela. It is not an application added to an existing BSP. The tri-core and
paired-image differences are real platform constraints and are explicitly
documented in the compliance review.

## 3. Repository layout

| Path | Purpose |
|---|---|
| `chips/bk7258/` | CP/AP/CPU2 code, IRQ, timer, peripheral wrappers, BL1/BL2, and chip Kconfig |
| `boards/bk7258/` | Three physical boards, paired CP/AP configs, partition CSVs, shared linker scripts, and bring-up |
| `tools/bk7258/` | Sole maintainer entry for toolchain, SDK bundles, build, signing, packaging, and verification |
| `tests/bk7258/` | Host regression that compiles active sources directly; not a firmware or hardware substitute |
| `docs/platforms/bk7258/` | Porting reports, compliance notes, debug procedures, and historical stage records |
| `progress/verification/` | Dynamic acceptance records with explicit build identity and applicability |
| `logs/lijian/` | Competition-format AI Coding JSONL logs |
| `logs/bk7258-*` | Early raw hardware evidence; not AI conversation logs |
| `prebuilt/` | Local locked-toolchain installation; generated binary content is ignored |
| `chips/bk7258/bk_idk/armino_as_lib/` | Local bundles rebuilt from the manifest-pinned SDK; third-party binaries are not redistributed |

The manifest maps maintained chip, board, tool, and application paths into the
standard openvela workspace. `tests/`, `docs/`, `progress/`, and `logs/` are
team-repository verification and delivery material and do not need NuttX source
tree mappings.

## 4. Build and run

### 4.1 Fetch the complete workspace

The command below selects both the default projects and the BK7258 SDK group
explicitly. The SDK project has no `notdefault` marker, so an ordinary default
sync also includes it; spelling out the group makes the reproduction input
unambiguous.

```bash
repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian \
  -b dev-ai-contest-2026 \
  -m contest2026_135_yongwangzhiqian.xml \
  -g default,bk7258-sdk
repo sync -c -j8
cd contest2026_135_yongwangzhiqian
```

Ubuntu 22.04 is recommended. Install the normal openvela host dependencies,
Python 3, CMake, Ninja, and GNU Make first. The Arm compiler is never selected
from the host `PATH`.

### 4.2 Install the locked toolchain

The tool downloads the archive from the official Arm HTTPS URL recorded in
`tools/bk7258/toolchain.json`, verifies its SHA-256, and installs it below the
ignored `prebuilt/` directory. Pass `--archive` to use an already-downloaded
copy of the same archive.

```bash
tools/bk7258/bk7258.py toolchain install
tools/bk7258/bk7258.py toolchain verify
```

### 4.3 Rebuild the SDK bundles

The manifest pins the SDK source at `vendor/beken/bk_avdk_smp`. T5-Board and
T5AI-Core use `cp` plus `ap`; AIDK AI Toy uses `cp-aidk` plus `ap`.

```bash
tools/bk7258/bk7258.py sdk rebuild \
  --profile cp --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk rebuild \
  --profile ap --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk verify --profile cp
tools/bk7258/bk7258.py sdk verify --profile ap
```

Replace `cp` with `cp-aidk` before building AIDK AI Toy. If a prepared bundle
matching the tracked hash is available, it may instead be loaded with
`sdk install --profile <name> --bundle <path>` and must still pass `sdk verify`.

### 4.4 Build the paired CP/AP system

The direct mode below is intended for unsigned bring-up and reproduction:

```bash
tools/bk7258/bk7258.py build \
  --board t5ai_core --boot direct --jobs 8
```

`t5_board` and `aidk_ai_toy` are the other board names. The entry reads the
board-owned `openvela.conf`, generates private CP/AP build configurations and
partition linker inputs, and invokes the official `build.sh ... --cmake` once
per role. `BK7258_SDK_DIR`, toolchain, and partition environment variables are
validated internal wrapper contracts and must not be set manually.

The command prints the exact build manifest, CP/AP ELF and raw binary paths,
final Flash-segment paths, and SHA-256 values. This is a multi-image layout, so
artifacts are named by partition role, such as `boot.bin`, `cp.bin`, `ap.bin`,
`pair.bin`, and `bl2-a.bin` in a signed release. The generic single-image name
`vela_ap.bin` does not describe this product.

`--boot mcuboot` selects the signed release chain. It requires newly generated
BL1 and MCUboot public keys for that release, private-key-side release steps,
and a strictly increasing rollback counter. Historical private keys must never
be reused. See the [build/flash/debug SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md)
for the complete process and Flash-write boundaries.

### 4.5 Host regression

After installing the `cmocka` development package, run this from the team
repository root:

```bash
./tests/bk7258/run_tests.sh
```

Success ends with `BK7258_HOST_TEST_PASS`. It proves only the mocked logic and
ABI covered by the suite; hardware capabilities require a matching board
evidence record.

## 5. AI Coding usage

AI assisted with requirement decomposition, cross-checking official and SDK
sources, startup and interrupt root-cause analysis, implementation and test
generation, hardware-log interpretation, threat modeling, and documentation.
AI conclusions were treated as hypotheses: ownership, partitions, symbols,
build artifacts, and hardware results were accepted only after reproducible
commands or raw evidence confirmed them.

Competition-format conversations are stored under
`logs/lijian/<date>/<tool>__<sid>.jsonl`, with the session index in
`logs/lijian/manifest.json`. `logs/bk7258-*` contains early serial and
secure-boot evidence and is not AI log data. New structured hardware
conclusions belong under `progress/verification/`.

## License

Unless a file or directory states otherwise, original content in this
repository is licensed under the Apache License 2.0; see [`LICENSE`](LICENSE).
Third-party and upstream-derived material remains subject to its original
copyright and license notices. Projects referenced by the manifest but not
stored in this repository are governed by their own licenses. See
[`tests/bk7258/PROVENANCE.md`](tests/bk7258/PROVENANCE.md) for the categorized
provenance of the BK7258 host-test sources.

## Review entry points

- [Official compliance review (English)](docs/platforms/bk7258/official-compliance-review.en.md) /
  [中文](docs/platforms/bk7258/official-compliance-review.md)
- [openvela documentation adaptation matrix](docs/platforms/bk7258/openvela-document-adaptation-matrix.md)
- [BK7258 board configuration and architecture](boards/bk7258/README.md)
- [BK7258 host tests](tests/bk7258/README.md)
- [AI Coding log format](logs/README.md)
