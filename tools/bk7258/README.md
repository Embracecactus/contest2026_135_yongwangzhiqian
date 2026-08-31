# BK7258 maintainer CLI

`bk7258.py` is the only tracked BK7258 maintainer entry.  It builds, signs,
verifies, packages and deploys artifacts; command implementations live in
`_lib/`.

## Source-layer gate

Every `bk7258.py build` runs the board/chip/app ownership gate before it
configures either role.  It can also be run directly:

```sh
python3 tools/bk7258/bk7258.py verify layers
```

The gate rejects raw Beken SDK headers, calls and types in `boards/bk7258` or
`app/bk7258`; chip-to-board dependencies; physical pin/bus ownership in app;
CP-only Kconfig symbols nested in AP-only menus (and the reverse); and new
product GATT/UUID policy in the chip layer.  Product protocol belongs in app,
physical and calibration facts belong in board, and SDK/controller mechanics
belong in chip.

`layer_exceptions.json` contains only hash-bound legacy product-protocol files.
Changing one invalidates the gate and requires a deliberate layer review; it
is not a wildcard allowlist.  `tests/host/bk7258/test_bk7258_layers.py` injects
each forbidden dependency and verifies that the gate fails closed.

## OTA deployment

`deploy` streams a signed CP/AP OTA package through the native USB CDC port,
then uses the CH340 CP console to reboot and confirm the accepted generation.
It is the host peer of the chip-level `BK7258_OTA_SOURCE_USB` source.

```sh
python3 tools/bk7258/bk7258.py deploy --inspect-only --package FILE \
  [--expected-board NAME] [--expected-version V] [--expected-counter N]

python3 tools/bk7258/bk7258.py deploy --package FILE \
  [--ota-port PORT] [--control-port PORT]
```

Use `--status-only` or `--reboot-only` with `--expected-version`,
`--expected-counter`, and a CH340 control port to check an accepted package.
`--control-port none` stages the pair without rebooting it.  Python 3 and
`pyserial` are required only when a serial port is opened.

The signed catalog may be scoped with `--expected-board`; product automation
must always supply its selected physical board.
