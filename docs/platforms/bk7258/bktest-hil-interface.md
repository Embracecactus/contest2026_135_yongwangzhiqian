<!-- SPDX-License-Identifier: Apache-2.0 -->
# BKTEST engineering HIL interface

BKTEST is a development-build input and observation interface for unattended
tests of the existing BK7258 product state machines. It is not a product
feature, a shell, a provisioning path, a power primitive, or evidence of a
physical K2 press.

## Ownership and security boundary

BKTEST reuses the native USB product-control connection. The connection keeps
the existing pinned device certificate, independent PC credential, SDC1
authentication, serialized request sequence, live grant revision, and owner
binding checks. A caller additionally needs the existing
`BKPC_CAP_DIAGNOSTICS` grant. Opening the CDC port, selecting it in a browser,
or knowing a USB serial number grants nothing.

`CONFIG_BK7258_ENGINEERING_TEST` owns the implementation and defaults to `n`.
Normal product and release profiles do not select it, and their diagnostics
capability remains unbound. An engineering image reports the mode in every
BKTEST status response. While that image is running, its PM peer is fail-closed:
an engineering K2 sequence can reach the real product coordinator and all real
resource-exit participants, but the final CP power request is replaced at the
external PM boundary by a bounded test peer. Physical K2, factory-reset erase,
OTP/eFuse, trust roots, owner data, Wi-Fi/cloud credentials, and external SD
are never changed by enabling or querying BKTEST.

## Production path under test

```text
authenticated SDC1 CONFIG kind 19
  -> BKT1 command validation
  -> engineering KEY1 source
  -> the same AP key event handler and K2 policy
  -> the same product power coordinator
  -> real admission close and resource quiesce
  -> engineering PM peer at the existing AP-to-CP dependency boundary
  -> existing atomic power snapshot / CP-visible bkhealth power query
```

BKTEST does not call `bk7258_pm_soft_off_request()`, reset, sleep, shutdown, or
factory erase from its command handler. It cannot mark resources drained or
change the product power state directly. The PM peer only returns the external
outcomes that a real CP can return: declined, response unknown, accepted and
pending, or a late acknowledgement. The coordinator remains the sole writer of
product lifecycle state.

## Version 1 wire contract

The existing SDC1 configuration transfer carries kind 19. `CONFIG_READ` returns
the public `BKS1` snapshot in 16-byte chunks. `CONFIG_BEGIN` accepts exactly one
32-byte `BKT1` record; normal SDC1 APPEND/APPLY rules provide bounds,
authentication, serialization, and one-shot consumption.

`BKT1` is eight big-endian words: magic `BKT1`, version 1, operation, nonzero
session, strictly increasing sequence, value, elapsed milliseconds, and flags
(zero in version 1). Operations are:

1. `session`: start a finite engineering input session and select the PM peer.
   It explicitly feeds a released baseline into the common event layer.
2. `key`: feed one normalized key mask. K2 is bit 1; K1/K3 retain their normal
   bit positions. Invalid masks are rejected before the handler.
3. `advance`: advance only the engineering input clock. It emits no held event.
4. `end`: release the engineering source. It never reopens product resources
   after a shutdown failure.

The status snapshot reports mode enabled, active session/sequence/logical time,
last key mask, PM mode and request/query counts, the public product power phase
and error, and the last BKTEST command result. Reads have no side effects and
return no credential, owner, network secret, model data, or filesystem path.

The four PM modes are fail-closed, declined, response unknown, accepted and
pending, and late ACK. Late ACK changes only the simulated peer response; it
cannot overwrite a coordinator terminal state. A session expires after a
bounded idle interval and must begin from a fresh released baseline.

## Host and board evidence

Exact 2999/3000/3001 ms edges use the deterministic host clock. The board HIL
uses the same BKT1 commands, observes the product power snapshot with the
existing read-only `bkhealth power` command on the CH340 console, and reboots
through the maintained HIL recovery path between destructive-to-runtime
scenarios. The HIL result is JSON containing the command, input sequence,
firmware version/build/counter, build-manifest source commit, result, and
failure reason. Raw serial transcripts remain separate evidence.

These layers remain distinct:

- host contracts prove exact time boundaries, ordering, duplicate rejection,
  CP timeout/reconciliation, and that production builds leave kind 19 unbound;
- engineering HIL proves the deployed image routes authenticated commands into
  the production event/coordinator path and exposes the resulting board state;
- physical K2 debounce/electrical behavior, real deep sleep/wakeup, acoustic
  playback, power consumption, phone BLE, and App OTA still need their own
  fixtures or a person at the device.

Future audio, resource, OTA, storage, and network engineering operations may
add new BKT versions or operations behind the same authorization and build
gate. They must keep their production state machine as the target and replace
only external peers or clocks needed for deterministic fault injection.
