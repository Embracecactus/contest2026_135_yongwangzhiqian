# BK7258 × openvela: three-core adaptation and Shaniu

[简体中文](README.md) · [Technical report (Chinese)](docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md)

One shared BK7258 SoC adaptation supports three physical boards. CPU0 runs
the CP NuttX image; CPU1 and CPU2 run a separate AP SMP image.
T5-Board runs the Dolphin utility application. AIToyBoard runs Shaniu,
a voice and vision companion using the official Agent, Session, Voice,
Media and Trigger components.

## Real-device videos

[![Project demonstration](docs/contest/assets/demo-cover.jpg)](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/download/shaniu-demo-20260920/shaniu-demo.mp4)

- [Main demonstration — 4:48](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/download/shaniu-demo-20260920/shaniu-demo.mp4)
- [Android controls — 1:26, supplementary](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/download/shaniu-demo-20260920/shaniu-app-demo.mp4)
- [Downloads, subtitles and hashes](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/tag/shaniu-demo-20260920)

The main video is under five minutes. These are edited demonstrations, not
continuous stress-test recordings. Distribution copies retain the complete
content and use 1080p H.264/AAC. The App video shows the OTA entry, not an
entire upgrade; OTA evidence is recorded separately.

## Product behavior

“你好，openvela” enters an interaction: local acknowledgement, automatic
capture, actual ASR, official Agent / selected LLM, TTS and speaker playback.
Follow-up questions do not require another wake word. Session termination
returns to hotword listening. ASR is batch, LLM returns complete text/tool
results, and TTS delivers audio chunks; this is not an all-streaming pipeline.

The device owns conversation execution. Android handles authenticated BLE
provisioning and settings, plus Wi-Fi HTTPS delivery of eye assets and signed
OTA packages. It does not relay conversation audio. Camera access uses the
existing single owner; native tools expose bounded device actions.
Optional persistent memory is encrypted and restored into official Sessions.

## Boards and build entry points

The [submission assets](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/tag/shaniu-demo-20260920)
include a Chinese report (PDF/DOCX), the 4:48 main video, a separate App demo,
four authentic board photos, an A2 poster and an editable 18-slide presentation.
Source and original AI logs remain in Git, not in the submission ZIP. Device
bootstrap secrets and device-bound recovery images are excluded. Artifact
preparation is not a completed website submission or upstream PR merge. The
packaged attachments (PDF/DOCX/PPT) predate the 637/638 updates and have not
been regenerated; the Markdown sources in this repository do not update them.

| Board | CLI ID | Application | SDK profiles |
|---|---|---|---|
| T5AI-Core V1.0.1 | `t5ai_core` | Platform baseline | `cp`, `ap` |
| T5-Board V1.0.2 | `t5_board` | Dolphin | `cp`, `ap` |
| AIToyBoard / AIDK AI Toy | `aidk_ai_toy` | Shaniu | `cp-aidk`, `ap-aidk` |

Each selects `app + openvela_ap` through its own `openvela.conf`.
Compiling does not require a physical board. Consult the
[board bindings](boards/bk7258/README.md) and [configuration contract](boards/bk7258/CONFIGS.md);
T5AI-EVB is not interchangeable with T5-Board.

**Publication boundary:** the working baseline is the official repository
`open-vela/contest2026_135_yongwangzhiqian` on `dev-ai-contest-2026`, which now
contains the F01-F12 remediation (`b72b8bbb..daacdc75`, 13 commits) and the
later 636/637 commits (`019a449e`, `faab4493`, `7d667565`). Source snapshot
`82610138` on `feat/shaniu-contest-delivery-20260920` was the historical fork
delivery path; a fork push is not an upstream merge or a completed contest
submission, and that content is merged into the baseline above.
The existing Agent changes are now published unchanged as
[`add0db19`](https://github.com/Embracecactus/packages_ai_agent/commit/add0db19d00301769907a5ece03fb9bd88d2edb4),
based on official `e65550f18759f086d7f544edcf17d1e31223244f` (21 files).
The team manifest pins that fork commit and 248 checked-out Linux dependency revisions.
No retired patch/overlay chain is restored. Public source availability is not an
upstream merge, a clean three-board build, or new board acceptance.
See [provenance](SOURCE_PROVENANCE.md).

On 2026-09-20 all three boards' CP/AP **direct builds passed** from an isolated
source checkout at `c10a7668`, using pinned dependencies and verified existing
SDK/toolchain caches. See the [build record and hashes](docs/verification/bk7258/2026-09-20-public-source-build.md).
These unsigned build checks were not flashed and do not replace the signed
635/637 packages.

Use Ubuntu 22.04 with the standard openvela build prerequisites:

```bash
repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian \
  -b dev-ai-contest-2026 \
  -m contest2026_135_yongwangzhiqian.xml -g default,bk7258-sdk
repo sync -c -j8
```

The main manifest supplies the Agent fork pin and the linkfiles; no team-project
override is needed. To re-examine the historical fork snapshot, use
`https://github.com/Embracecactus/contest2026_135_yongwangzhiqian` with
`feat/shaniu-contest-delivery-20260920`; that content is merged into the
baseline above and is not the current entry point.
Record `repo manifest -r` and dirty dependency state, then enter the team repository:

```bash
cd contest2026_135_yongwangzhiqian
tools/bk7258/bk7258.py toolchain install
tools/bk7258/bk7258.py toolchain verify
tools/bk7258/bk7258.py sdk rebuild --profile cp --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk rebuild --profile cp-aidk --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk rebuild --profile ap --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk rebuild --profile ap-aidk --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py build --board t5ai_core --boot direct --jobs 8
tools/bk7258/bk7258.py build --board t5_board --boot direct --jobs 8
tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct --jobs 8
```

Build boards sequentially. A single-board build needs only its two SDK profiles.
The owner has verified Dolphin recording WAV files to SD on T5-Board, including
the recorded `0.1.0+13` build. Commit `8de0ae78` restores the accidentally removed
Dolphin-only NuttX recorder build wiring and enables recording again. T5 CP/AP
builds, existing recorder host checks and ELF linkage checks passed. Subsequent
board verification found a GT9xx/LVGL input mismatch; `c6976458` fixes the adapter
and display/input initialization now passes. The TF problem recorded for that
candidate was later traced to card contact and recovered by re-seating; physical
touch and recording-to-SD with the new build remain unverified, and the recovery
does not prove recording passed.
See the [Dolphin record](docs/platforms/bk7258/dolphin-master-plan.md).
Firmware 638 is the current verified Shaniu build (see the verification status
above); the 634/635/637 records stay as history for their versions.
Keep the team checkout directory name specified by the manifest; SDK tools read
its same-named XML. AIToyBoard requires both `cp-aidk` and `ap-aidk`.
The manifest pins the SDK to `cb080de1655d579c7593ecf504c440997c4c137b`.
Outputs and source/configuration hashes are recorded in the emitted build manifest.
`direct` is unsigned bring-up, **not** an update for a provisioned secure device.
For signed builds and device-safe deployment, use the
[existing release SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md).
Private keys and device-specific recovery images are not public build dependencies.

## Verification status (2026-09-20)

Every claim names its version and evidence layer; "the current HEAD passes" is
not used as a permanent statement.

- **Source-layer gate** (static, not board evidence): `bk7258.py verify layers`
  PASS (500 sources / 252 Kconfig / 2 hash-bound legacy exceptions); the Agent
  orchestrator and trigger backend pass the pinned nxstyle with 0 findings.
- **T5-Board** (unsigned direct chain): four segments downloaded; boot reaches
  `SYSINIT/FINALINIT/RCS PASS`, NSH and dolphin-ui start; the SD failure was a
  TF card-contact problem, confirmed by re-seating.
- **AIDK AI Toy**: signed `v18.6.401+638` full image (operator 8,388,608 B,
  SHA256 `33c387c1…1cfc`; `.bkpack` 7,980,186 B) built and package-verified
  from clean source `dc06613d`.  Its CP/AP payloads are byte-identical to 637,
  and the two images differ in 659 bytes, all inside counters and signature
  regions.  The owner flashed it and confirmed wake → the "我在" acknowledgement
  (the 31,208-byte recording played) → ASR → LLM → TTS → playback → follow-up
  capture without a second wake word → silence timeout back to standby; one ASR
  request failed transiently with `ret=-5` and later requests succeeded in the
  same observation window; the excerpt cannot prove whether a standby or a
  second wake happened in between.  App OTA was not retested on 638 and still cites the 634 result.
  Artifact hashes and layers:
  [638 verification record](docs/verification/bk7258/2026-09-20-shaniu-638-full-image.md);
  previous generation of the same content:
  [637 record](docs/verification/bk7258/2026-09-20-shaniu-637-full-image.md).
- **Full-image boundary**: `release full` materializes the operator image from a
  same-unit readback base, so it carries that unit's device-bound persistent
  data and stays a same-unit recovery artifact. It is not a published
  general-purpose first-flash image and must not be flashed on another board;
  the factory-init path is not verified. Reviewers and judges build from source
  ([`tools/bk7258/README.md`](tools/bk7258/README.md), "First complete flash").

`wake_reply.pcm` (31,208 B) is a private acknowledgement recording admitted for
the contest only; public configurations keep it disabled and it is removed
after the contest. Device bootstrap secrets and device-bound recovery images are
not part of the public deliverables.

## First deployment: source to first full run

The Chinese root README carries the authoritative step-by-step chapter
("评审快速开始"); the per-input sources, consumers, install locations and
success criteria are in the
[first-deployment input list](docs/platforms/bk7258/first-deployment-inputs.md).
Three boundaries apply before anything else:

1. **The operator image is device-bound.** `release full` materializes it from a
   same-unit readback, so a reviewer must run `package accept-base` on their own
   board and never flash the author's image.
2. **First-time `/data` initialization is explicit.** The firmware only runs
   `mount -t littlefs /dev/mtdblock0 /data` and never formats automatically; a
   board whose `persistent_data` is not LittleFS uses the CP command
   `bkdata init --confirm erase-non-littlefs`, which formats only content that
   is not already a valid LittleFS, and then reboots. This command has not been
   verified on a new board yet.
3. **Identity supply and App claiming are separate.** The device TLS identity
   (BPI1) is written through the CP `bkprov supply` command; the App then imports
   the same `owner-bootstrap.json` to claim the device over BLE.

Main line, in dependency order (full commands and criteria in the Chinese
chapter):

1. Identify the board (`aidk_ai_toy`), the CH340 console and whether the device
   is running or blank; a blank board needs the K1 Boot ROM window, never
   `reset reboot`.
2. `repo init/sync` from `open-vela/contest2026_135_yongwangzhiqian`
   `dev-ai-contest-2026`, then `toolchain install/verify` and
   `sdk rebuild/verify --profile cp-aidk|ap-aidk`.
3. Public assets: builtin KWS model `nihao_openvela.tflite` (23,640 B,
   `922eba91…`), the contest-only `wake_reply.pcm` (31,208 B), and the eye
   source `shaniu-cyan-v2.json`; build an installable `.bkep` with
   `package eye-pack` and check it with `verify eye-pack`. Reviewers need no
   author binary: the repository source generates it (stdlib only) and the
   expected result is 108,634 B / `050f1175…` with `pack_id=shaniu-cyan-v2`,
   `revision=2`, `source_sha256=9a161ad6…`.
4. Per-device private inputs: one EC P-256 device certificate/key pair per board
   (OpenSSL command in the Chinese chapter), generated outside the repository;
   the four-field `owner-bootstrap.json` is never hand-written, it is produced by
   `voice pairing --direct-cloud` together with the identity write. The device
   never overwrites a different stored identity (`EEXIST`) and there is no
   supported identity-clear entry, so a board claimed by someone else needs that
   owner's bootstrap file.
5. Storage state, build and image: confirm `BK7258 FINALINIT PASS`; bind the
   board's own readback with `package accept-base`; then `build --boot mcuboot`
   and `release full` to produce the 8-MiB operator image and `.bkpack`.
6. Flash with `bk_loader` (CH340, `--fast-link 1`; T5-Board reset rules do not
   apply to AIDK) and check `FINALINIT PASS` plus `AIDK DEFERRED DONE
   failures=0`.
7. `voice pairing --direct-cloud` writes `owner-bootstrap.json` (0600,
   four fields) and supplies the same BPI1 record via `bkprov supply`; verify
   with `bkprov status` (`identity=present`) and after a reboot.
8. Install the App, import the authorization file, claim over BLE, then submit
   Wi-Fi and cloud settings.
9. First interaction: wake → "我在" acknowledgement → ASR → LLM → TTS → playback
   → follow-up without a new wake word → standby.
10. Resource updates through the App: import a `.bkep` with "导入眼睛素材包",
    install it with "通过 Wi-Fi 安装所选眼睛", verify with "读取当前眼睛" and a
    display change, then re-check after a reboot; import a `.wkm` with
    "导入唤醒词模型" (builtin `nihao_openvela` for the review path) and verify the
    device's active model label/phrase/SHA256, not just the App selection.

Not yet closed: board verification of `bkdata init` and `bkprov supply` on a new
unit, and a full App-side resource-update readback on that unit. The packaged PDF/DOCX/PPT attachments
still predate the 637/638 updates.

## App, models, Skills and evidence

- [Firmware comparison release](https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/releases/tag/shaniu-firmware-20260920)
  (`shaniu-firmware-20260920`) ships the last board round `18.6.401+641` as
  device-data-free images (boot/BL2/CP/AP/pair/manifest), the build evidence,
  the debug APK (8,310,136 B), two installable `.bkep` packs and the three
  `.wkm` models, each with a SHA256 entry. The 8-MiB operator image and the
  full `.bkpack` are **not** published because both embed this unit's
  `payloads/persistent_data.bin`: device TLS private key, local Wi-Fi
  credentials and the cloud API key. Reviewers who want a flashable full image
  must materialize it from their own board's readback, as the quick-start
  chapter requires.
- [Android project](android/shaniu-companion/README.md): JDK 17, SDK 35,
  Android 10+, source version `0.5.23-shaniu-rebind` / code 28.
- [Model tools](tools/bk7258/README.md): existing `voice kws` commands.
  Training dependencies are not required for a normal firmware build.
  The public builtin model is 23,640 B / SHA prefix `922eba91`;
  the App-activated experimental 47,672 B model `536ebba8` is a different asset.
- [Eye assets](app/bk7258/assets/display/README.md): original atlas, metadata,
  pack/verify commands. The private acknowledgement PCM (31,208 B) is admitted
  for the contest build only, disabled in public configurations, and removed
  afterwards.
- [Reusable development Skills](docs/platforms/bk7258/shaniu-skill-capability-map.md).
  Runtime `device-assistant.md` was verified on firmware `18.6.399+635`:
  installation log, a 2,012-byte tool table, user-confirmed voice and display.
  See the [635 evidence](docs/verification/bk7258/2026-09-20-shaniu-runtime-skill-635.md).
- [Master Plan and exact board evidence](docs/platforms/bk7258/shaniu-master-plan.md).
  Actual App OTA to `18.6.398+634` was confirmed after reboot (counter 634,
  slot B, confirmed trial). Reinstalled eyes persisted across reset.
  App controls and App OTA were not rerun on 635; their unchanged source paths
  do not constitute new acceptance. Deploying the 635 full package raises its
  bootloader floor to 635; the OTA-only package does not replace BL1/BL2.
  The current build is `18.6.401+638` (floor 638, same-unit recovery image,
  byte-identical CP/AP payloads to 637); its App OTA path was not retested
  either.
  Historical upgrade paths, storage faults and independent human wake-word
  generalization are not thereby certified.

App and model tooling remain versioned in this repository. The official
`packages/ai_agent` is a separate dependency, not a missing team-owned repo.
Retired test applications stay out of the Shaniu product configuration;
historical verification records retain their original boundaries.
See the [documentation index](docs/README.md), [AI logs](logs/README.md)
and [source/license provenance](SOURCE_PROVENANCE.md).
Original code is licensed under [Apache-2.0](LICENSE).
