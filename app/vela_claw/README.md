# Vela-Claw

A clean **openvela / Apache NuttX-native** port of Espressif's
[esp-claw](https://github.com/espressif/esp-claw) "Chat Coding" AI agent,
targeting the **BK7258 t5board**. The CP core runs communication; the AP core
runs applications — and Vela-Claw runs on the AP core.

Vela-Claw turns natural-language requests into action by asking a (cloud) LLM
for a **tool call**, executing that tool through an on-device **capability**
system, and feeding the result back until it converges on a final answer.

## Architecture

```
   ┌────────────┐      ┌────────────┐
   │ Serial CLI │      │ Screen UI  │   input channels
   │ (NuttX     │      │ (LVGL +    │
   │  console)  │      │  UIKit)    │
   └─────┬──────┘      └─────┬──────┘
         │  claw_event        │
         └─────────┬──────────┘
                   ▼
           ┌───────────────────┐
           │  event router      │  matches rules -> run_agent / send_message
           └─────────┬──────────┘
                     ▼
           ┌───────────────────┐
           │  agent core        │  worker thread: perceive -> decide -> execute
           │  (claw_core)       │  iterates tool calls until a final answer
           └─────────┬──────────┘
                     ▼
        ┌────────────────────────────┐
        │ capability system (tools)  │  lua_run_script, files, system, ...
        └─────────┬──────────────────┘
                  ▼
        ┌────────────────────────────┐
        │ LLM transport (pluggable)  │  mock (host/tests) | curl (real HW)
        └────────────────────────────┘
```

- **Input channels are just event producers.** The serial CLI and the screen
  UI both normalize input into a `claw_event` and push it to the same router.
  Adding a channel never touches the core.
- **RTOS-portable core.** `claw_rtos.c` abstracts threads/mutex/sem/queue over
  POSIX (host) and NuttX, so the whole agent loop compiles and runs unchanged
  on the host for tests.
- **Pluggable transport.** The LLM client depends only on `claw_transport_t`.
  The `mock` backend returns scripted responses so the full loop is verifiable
  offline; `curl` (libcurl4nx on NuttX) reaches a real cloud LLM.

## Build / test on the host

The agent core is host-compilable with no NuttX, LVGL, or libcurl:

```sh
make -C app/vela_claw/tests run
# expects:  ALL TESTS PASSED
```

The smoke test (a) calls `lua_run_script`/`led_blink` directly and (b) drives a
full `CLI submit -> mock LLM tool_call -> capability -> final answer` loop,
asserting convergence.

## Enabling on real hardware (app-layer config)

All hardware features are switched on **at the app/config layer** (no kernel
fork needed). In `make menuconfig`:

- `BK7258_APP_VELA_CLAW` — build the Vela-Claw builtin app.
- `VELA_CLAW_UI` — pulls in `GRAPHICS_LVGL` + `UIKIT` + `BK7258_LCD` +
  `BK7258_GT1151` (`INPUT_GT9XX` / `I2C_BITBANG`) for the screen UI.
- `VELA_CLAW_NET` — pulls in `NET` + `BK7258_WIFI_VNET` + `LIBCURL4NX` for the
  real cloud LLM.

A ready fragment is in `app/vela_claw/defconfig.vela_claw.inc`.

## Mapping to esp-claw

| esp-claw            | Vela-Claw                              |
|---------------------|----------------------------------------|
| `claw_event_router` | `src/router/claw_event_router.c`       |
| `router_rules.json` | `scripts/router_rules.json` (seed)     |
| capabilities/tools  | `src/cap/*` + `claw_cap_registry.c`    |
| Lua execution       | `src/lua/*` (`led_blink` driver)       |
| transport           | `src/transport/*` (mock | curl)         |
| memory              | `src/memory/claw_memory.c` (KV-backed) |
| agent loop          | `src/core/claw_core.c`                 |

## Recovery / persona seeds

- `scripts/recovery/soul.md` — agent persona.
- `scripts/recovery/MEMORY.md` — long-term memory seed (device + user facts).
- `scripts/recovery/skills/` — example skills (e.g. `blink_led.md`).

## Status / caveats

- Host smoke test passes; the core loop is verified.
- The screen UI (`src/ui/claw_ui.c`) is written version-tolerant
  (`#if LV_VERSION_MAJOR >= 9`) because LVGL source is pulled at build time.
  It must be compile-validated against the actual LVGL version when the
  firmware image is built.
