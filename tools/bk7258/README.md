# BK7258 maintainer CLI

`bk7258.py` is the only tracked BK7258 maintainer entry.  It builds, signs,
verifies, packages and deploys artifacts; command implementations live in
`_lib/`.

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

The signed catalog may be scoped with `--expected-board`; the AIDK pipeline
always supplies its selected physical board.
