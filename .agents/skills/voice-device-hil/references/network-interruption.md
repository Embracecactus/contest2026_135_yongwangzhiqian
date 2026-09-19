# Controlled network interruption

This procedure applies when an authorized, reversible upstream control exists
and current credentials/configuration can be preserved. It is not a general
Wi-Fi provisioning method or a guarantee that every phone supports STA+AP.

## Choose a path without destroying the baseline

1. Locate the **actual network owner**. In a multicore device the visible
   console may belong to another core. `ifdown` on a nonexistent or unrelated
   interface is not an interruption of the product's live network.
2. Read the existing console/API implementation before a supposedly read-only
   status command: it may synchronize leases or trigger recovery. Do not use
   such a command while claiming reconnection was automatic.
3. Prefer an existing reversible upstream switch. A temporary phone hotspot is
   conditional on OS capability, ordinary UI support and preservation of its
   current state. Read only authorized hotspot fields. Keep any necessary
   secret backup local with restrictive permissions and out of tool output.
4. If shell hotspot control raises an OS permission exception, use the normal
   Settings UI only when authorized. Do not add privileged services or root
   the phone. Confirm the actual applied SSID/security/state after input;
   IME composition and stale coordinates can silently change text.
5. Move the board only via an existing **runtime-only** connection operation
   whose implementation is known not to overwrite persistent preferences.
   Otherwise use the product's approved configuration/restore path or stop
   this branch. Never invent an empty password or persistence flag.

## Observe the intended failure

Record successful baseline association/DHCP and product readiness. To test an
active-request timeout, first obtain an accepted physical wake and observe the
request in flight. Turn off the upstream only after that marker; if no wake
occurs, report the active-request test **not executed**.

Record disassociation/network failure, request timeout/cancel, resource release,
and subsequent idle state. Restore the upstream and observe association/DHCP
and product recovery **without** issuing a board reconnect/status command with
side effects. Then require a fresh complete voice turn. Idle Wi-Fi recovery
and recovery from an interrupted request are separate gates.

## Restore and stop

Restore the observed phone/router settings including security, credentials,
band and on/off state. Verify sensitive equality in memory and output only
aggregate match results. Remove the temporary secret backup after verified
restoration; do not archive it with evidence.

Use the existing runtime disconnect/return-to-saved-network path if verified.
If it remains busy after a bounded retry, inspect ownership; do not label it
successful or loop indefinitely. An authorized normal reboot may restore
persistent network settings, but report it as a recovery intervention, not
proof that runtime reconnection worked. Verify saved identity/configuration,
firmware version and readiness after any restart.

Stop the affected operation if the target is ambiguous, restoration cannot be
guaranteed, or required privilege/manual control is unavailable. Continue
unaffected development and preserve the failure evidence.
