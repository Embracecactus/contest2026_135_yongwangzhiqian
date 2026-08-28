# BK7258 Platform Integration

English | [简体中文](README.md)

This is the shared platform-integration entry for T5AI Core, T5 Board, and AIDK
AI Toy. It covers paired CP/AP builds, delivery compliance, debugging procedures,
and retained engineering-stage records.

> **Current-status correction (2026-08-10):** The custom N15/N17 OTA selector,
> writer, journal, validation profiles, and scripts have been retired from the
> maintained source. Their records are historical evidence only. The current
> boot chain is board-owned BL1 → pinned NuttX MCUboot BL2 → signed same-slot
> CP/AP images; it provides no field OTA writer, confirm, or rollback service.

Use the following sources according to scope:

- [Official compliance review](official-compliance-review.en.md) for the exact
  interpretation of openvela documents 1443, 1444, and 1445;
- [Chinese platform index](README.md) and the [porting report](porting-report.md)
  for implementation details and historical stage links;
- [BK7258 SoC documentation](../../chips/bk7258/README.md) for contracts that do
  not depend on a physical board;
- [`boards/bk7258/`](../../../boards/bk7258/) for board pinout, profiles, and
  partition selection; and
- [`progress/CURRENT.md`](../../../progress/CURRENT.md) plus matching verification
  records for current acceptance status.

The detailed N1–N17 material retained below the Chinese index is historical
engineering evidence. It must not be read as a claim that every historical
profile remains part of the current product configuration.
