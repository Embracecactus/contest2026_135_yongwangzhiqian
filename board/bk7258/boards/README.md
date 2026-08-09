# BK7258 physical-board variants

`board/bk7258` owns the shared BK7258 chip, CP/AP, boot-chain, SDK-wrapper,
partition and build integration.  This directory contains only physical-PCB
wiring and capability facts.

## Naming

Stable directory and Kconfig names identify the board product, not a PCB
revision:

| Physical board | Directory | Kconfig | Documented revision |
|---|---|---|---|
| T5AI-Core | `t5ai_core` | `CONFIG_BK7258_BOARD_T5AI_CORE` | V1.0.1 |
| T5-Board | `t5_board` | `CONFIG_BK7258_BOARD_T5_BOARD` | V1.0.2 |

Hardware revisions live in `BK7258_BOARD_HARDWARE_VERSION`.  A revision gets
its own selector only if it changes a software-visible electrical contract.
T5AI-Core is the default so all existing configurations retain their verified
board behavior.

## Source boundary

The current pin facts come from these schematics supplied by the project
owner:

- `T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf`
- `T5-Board_V102_SCH250617.pdf`

TuyaOpen's `TUYA_T5AI_CORE` confirms the Core board's P9 LED, P29 key and P39
speaker-control naming.  TuyaOpen's `TUYA_T5AI_EVB` is not electrically
equivalent to T5-Board V1.0.2 and is not used as a pin source.

## Board-level mapping

| Function | T5AI-Core V1.0.1 | T5-Board V1.0.2 |
|---|---:|---:|
| User LED | P9, active high | P1, active high |
| User key | P29, active low | P28 (`ADC_KEY`), active low |
| Speaker control | P39 | P9 |
| Battery ADC | P28 | not fitted |
| Charge detect | P38 | not fitted |
| TF card | not fitted | CLK P8, CMD P3, D0 P4, D1 P5, D2 P10, D3 P11, CD P6 |
| RGB LCD connector | not fitted | fitted |
| DVP camera connector | not fitted | fitted |

The Core board is the hardware-verified baseline.  T5-Board entries above are
schematic-verified only until their corresponding peripherals are exercised on
that physical board.  Variant selection does not automatically enable every
fitted peripheral; Kconfig still controls driver ownership and pin-compatible
profiles.
