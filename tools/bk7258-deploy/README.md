# tools/bk7258-deploy

Deliver verified BK7258 OTA packages to a physical device.  This is the peer
of `tools/bk7258/bk7258.py`: that tool builds and signs one `.bkpack`, this
tool hands it to the device over a transport and confirms the result.

## Layout

| File | Responsibility |
|---|---|
| `bk7258_deploy.py` | CLI entry: argument parsing and the deploy orchestration |
| `deploy_usb.py` | USB CDC transport (host peer of the chip-level `BK7258_OTA_SOURCE_USB` source) |
| `deploy_console.py` | CH340 CP console: `bkota status/reboot` polling and paired-health confirmation |

Adding a transport means adding one module beside `deploy_usb.py` and wiring
it into `bk7258_deploy.py`.  The chip layer already owns file and HTTPS OTA
sources (`chips/bk7258/ap/bk7258_ota_source_{file,http}.c`), so a future
`deploy_file.py` or `deploy_http.py` only has to match the existing
`bk7258_ota_source_ops_s` consumers on the device (`bkota apply-file` /
`bkota apply-http`); no firmware change is required.

## Requirements

- Python 3 with `pyserial` in the environment that can open the serial ports
  (under WSL, point `AIDK_WINDOWS_PYTHON` at a Windows Python if needed).
- The package must come from `bk7258.py release ota` (signed against the
  MCUboot root compiled into the device); this tool never accepts a package
  the release tool would have rejected.

## Commands

Inspect a package without touching any hardware:

```sh
bk7258_deploy.py --inspect-only --package FILE \
  [--expected-board NAME] [--expected-version V] [--expected-counter N]
```

Stream a package over native USB CDC, then reboot through the CH340 console
and wait for the automatic confirmation:

```sh
bk7258_deploy.py --package FILE [--ota-port PORT] [--control-port PORT]
```

Poll status or only reboot through the CH340 console:

```sh
bk7258_deploy.py --status-only  --expected-version V --expected-counter N --control-port PORT
bk7258_deploy.py --reboot-only  --expected-version V --expected-counter N --control-port PORT
```

`--control-port none` stages the pair over USB without opening the console.

## Board scoping

`--expected-board` (optional) requires the signed catalog to target that
physical board.  Omit it to accept any BK7258 board the package declares.
The wrapper in `boards/bk7258/aidk_ai_toy/scripts/aidk_pipeline.sh` always
passes `--expected-board` derived from its own `--board` option.
