---
name: android-device-session
description: Repair or implement shared foreground Android BLE/GATT control sessions, authentication lifetimes, serialized device commands and truthful UI state. Use when tabs or Activity transitions lose a device, polling disables controls, or command failures look like disconnects. Not for an unlimited background service, a replacement BLE stack, or physical-device acceptance alone.
---

# Android device control session

## Inputs

Read the existing Activity/navigation owner, connection bridge, GATT/TLS
session, protocol and board-side timeout/control implementation. Obtain build,
package ID and current device-management behavior from the project; do not
assume a particular timeout, volume range or persistent-service requirement.
Reuse the existing Android architecture and transport rather than introduce a
second connection framework. Installation/device actions need current scope.

## Diagnose and implement

1. Correlate current logs with tab switches, continuous foreground connection
   beyond the original timeout, and leaving for another Activity then returning.
   Distinguish GATT/TLS close, authentication expiry, failed STATUS and UI-only
   disconnected rendering. Log only generation, phase, command type, duration,
   close reason and disable reason when existing logs are insufficient.
2. Put the effective session owner at the lifetime that spans all foreground
   tabs. The UI renders/subscribes to one state source; tab changes must not
   scan, authenticate or construct another GATT. A thin file move without
   changing transport ownership is not a fix.
3. Separate provisioning's absolute transaction limit from authenticated
   control's lifecycle. Preserve handshake, write and command deadlines.
   Promote only after authentication; if control uses an idle lease, renew
   only on verified protocol activity, not arbitrary ATT bytes. Check the
   board for the same absolute-limit bug. Do not globally disable timeouts.
4. Model connection/authentication, snapshot freshness, read request, write
   request and single-operation result independently. A failed operation
   leaves link state intact unless transport/auth evidence says otherwise.
   Retained data must be labeled stale, not freshly confirmed.
5. Keep one protocol request in flight. Queue user actions; coalesce or defer
   background reads behind writes. Polling must not repeatedly lock every
   control. Do not bypass an existing pending flag to create concurrent
   commands in a single-request protocol.
6. Guard callbacks with generation/request tokens. Invalidate them **before**
   closing an old transport; clear pending payloads safely. Copy data across
   asynchronous ownership boundaries and scrub sensitive buffers when no
   longer needed. Old accept/close/result events must not mutate the new state.
7. Have one reconnect job. Stop it on explicit user disconnect; reauthenticate
   and read fresh status on recovery. Clear old mutations, claims, deletes and
   OTA actions rather than replaying them. Use bounded/backoff scheduling and
   an explicit foreground/background policy. Returning from a chooser is not
   a request for an infinite background service.
8. Trace each setting end to end. For volume: UI disable reason → snapshot
   availability → STATUS serializer → board control → preferences → actual
   playback gain. If playback/capture rejects changes, expose that limitation
   until the backend supports it; do not just remove the UI busy check.
9. Treat a mutation ACK as acceptance only. Schedule a status read after it;
   confirm success only when the reported device value matches. On error or
   unknown readback keep the last verified value and state the uncertainty.

## Validation and failure branches

Build the affected Android target with existing commands. Use existing fake
clock facilities for the original time boundary, preserving provisioning's
deadline; do not add a test system simply to execute this skill.

On an available authorized phone/device, retain application data and identity:

- One connection, a bounded set of tab round trips covering the affected
  transitions: use the project's acceptance count and compare connection
  generations and scans, not merely screenshots.
- Foreground beyond the old expiry and external Activity/chooser return:
  fresh status and allowed controls still work.
- Failed command, unavailable status and actual disconnect: each renders its
  own state; unknown volume, busy device and pending write explain different
  disable reasons.
- Two moderate supported volume values: ACK, readback and physical playback
  are separate gates. Restore the observed baseline if the exercise requires
  it; do not assume a historical value is the current baseline.
- Reconnect reads fresh state without replaying previous writes; affected
  provisioning, OTA supply and cancellation paths retain their behavior.

If link logs stay healthy while the UI says disconnected, inspect state
mapping before reconnect code. If a setting stays grey, record its exact
guard and device response before modifying it. Host/emulator checks do not
prove real BLE, authentication timing or speaker gain.

## Deliver

Provide the modified ownership/caller paths, APK identity and location,
selected build/check results, physical results and unresolved limits in the
existing project record. Completion requires shared session behavior and
truthful setting confirmation, not just a successful build or enabled widget.
