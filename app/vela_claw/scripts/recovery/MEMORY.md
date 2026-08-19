# Vela-Claw — Long-Term Memory (seed)

This is the seed for Vela-Claw's on-device long-term memory
(`claw_memory_set/get_long_term`). Entries here are loaded on first boot so the
agent already knows the device it runs on.

## Device
- Platform: BK7258 t5board (openvela / Apache NuttX RTOS)
- Topology: CP core = communication, AP core = applications (you run here)
- Display: RGB LCD (ILI9488) driven over the BK7258 LCD controller
- Touch: GT1151 capacitive panel (`/dev/input0`)
- Network: Wi-Fi via CP vnet proxy, exposed to the AP as `wlan0`

## User preferences (learned)
- Prefers the screen UI as the primary channel; serial CLI kept for debugging.
- LVGL + openvela UIKit is the chosen UI framework (native, Chinese-capable).
- Cloud LLM is reached over Wi-Fi; offline/mock transport is used for tests.

## Capabilities available
- `lua_run_script` — run Lua that drives hardware (e.g. `led_blink(3)`)
- `cli`, `files`, `llm_config`, `system`, `mgrs`, `scheduler`,
  `http_request`, `web_search`

## Notes
- This file is a seed only. At runtime, long-term facts are persisted to the
  KV store under `/data/vela_claw/lt_*.txt`.
