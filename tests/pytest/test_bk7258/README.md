# BK7258 official pytest integration

The manifest links this directory to
`<workspace>/tests/scripts/script/test_bk7258`.  Run it from the official
OpenVela tests script directory so the parent fixture and utilities are used:

```bash
cd <workspace>/tests/scripts
pytest script/test_bk7258 \
  -D <UART0-device> -B <t5_board|t5ai_core|aidk_ai_toy> -U cp \
  -P <directory-containing-the-CP-.config> \
  -L <log-directory> -F /data -R target -M serial
```

`-D` is the selected board's UART0 CP NuttShell/debug port.  The test contains
one BK7258-wide boot contract and a board-keyed marker table; it does not infer
peripherals from one board.  Other UARTs and AP-owned peripheral transports are
never opened by the official fixture.  This suite is for a board's `xts` CP
image paired with that same board's normal `openvela_ap` image; it is not the
contract for `drivercheck`, performance, or production-only profiles.
