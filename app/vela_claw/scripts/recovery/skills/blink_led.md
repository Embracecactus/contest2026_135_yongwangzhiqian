---
name: blink_led
description: Blink the on-board status LED a given number of times. Use when the user asks to flash, blink, or pulse the LED.
type: skill
platform: bk7258
---

# Skill: blink_led

Blink the t5board status LED.

## When to use
The user says "blink the led", "flash the light", "pulse the indicator", etc.

## How to act (Lua via lua_run_script)
Emit a tool call whose arguments carry Lua source:

```lua
led_blink(3)   -- blink 3 times
```

- `led_blink(n)` toggles the LED GPIO `n` times.
- `n` defaults to 1 when omitted or non-positive.

## Example tool call
```json
{
  "name": "lua_run_script",
  "arguments": { "code": "led_blink(3)" }
}
```

## Notes
- The mock Lua engine (host/tests) recognizes `led_blink(n)` and reports a
  deterministic result so the agent loop is verifiable without hardware.
- On real hardware the same `led_blink` symbol is bound to the BK7258 GPIO
  driver and actually toggles the pin.
