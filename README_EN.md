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

| Board | CLI ID | Application | SDK profiles |
|---|---|---|---|
| T5AI-Core V1.0.1 | `t5ai_core` | Platform baseline | `cp`, `ap` |
| T5-Board V1.0.2 | `t5_board` | Dolphin | `cp`, `ap` |
| AIToyBoard / AIDK AI Toy | `aidk_ai_toy` | Shaniu | `cp-aidk`, `ap-aidk` |

Each selects `app + openvela_ap` through its own `openvela.conf`.
Compiling does not require a physical board. Consult the
[board bindings](boards/bk7258/README.md) and [configuration contract](boards/bk7258/CONFIGS.md);
T5AI-EVB is not interchangeable with T5-Board.

**Publication boundary:** source snapshot `82610138` is on
`feat/shaniu-contest-delivery-20260920`, based on official `7079493e`.
A fork push is not an upstream merge or completed contest submission.
The Shaniu product still depends on unpublished local changes to
`packages/ai_agent@e65550f18759f086d7f544edcf17d1e31223244f`.
Consequently, a clean public-manifest build of the complete product is
**not yet established**. No retired patch/overlay chain is supplied as a workaround.
See [provenance](SOURCE_PROVENANCE.md).

Use Ubuntu 22.04 with the standard openvela build prerequisites:

```bash
repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian \
  -b dev-ai-contest-2026 \
  -m contest2026_135_yongwangzhiqian.xml -g default,bk7258-sdk
repo sync -c -j8
```

Before the delivery branch is merged, apply the team-project-only local
manifest shown in the [Chinese build guide](README.md#评审构建指南).
It does not resolve the Agent dependency gap.
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
Keep the team checkout directory name specified by the manifest; SDK tools read
its same-named XML. AIToyBoard requires both `cp-aidk` and `ap-aidk`.
The manifest pins the SDK to `cb080de1655d579c7593ecf504c440997c4c137b`.
Outputs and source/configuration hashes are recorded in the emitted build manifest.
`direct` is unsigned bring-up, **not** an update for a provisioned secure device.
For signed builds and device-safe deployment, use the
[existing release SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md).
Private keys and device-specific recovery images are not public build dependencies.

## App, models, Skills and evidence

- [Android project](android/shaniu-companion/README.md): JDK 17, SDK 35,
  Android 10+, source version `0.5.23-shaniu-rebind` / code 28.
- [Model tools](tools/bk7258/README.md): existing `voice kws` commands.
  Training dependencies are not required for a normal firmware build.
  The public builtin model is 23,640 B / SHA prefix `922eba91`;
  the App-activated experimental 47,672 B model `536ebba8` is a different asset.
- [Eye assets](app/bk7258/assets/display/README.md): original atlas, metadata,
  pack/verify commands. Private acknowledgement PCM is optional and excluded.
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
  Historical upgrade paths, storage faults and independent human wake-word
  generalization are not thereby certified.

App and model tooling remain versioned in this repository. The official
`packages/ai_agent` is a separate dependency, not a missing team-owned repo.
Retired test applications stay out of the Shaniu product configuration;
historical verification records retain their original boundaries.
See the [documentation index](docs/README.md), [AI logs](logs/README.md)
and [source/license provenance](SOURCE_PROVENANCE.md).
Original code is licensed under [Apache-2.0](LICENSE).
