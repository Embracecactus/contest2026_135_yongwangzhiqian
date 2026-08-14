# ADR-025: Keep BK7258 radio lifecycle ownership explicit across SDK IPC

- Status: Accepted
- Date: 2026-08-14
- Decision owner: Project owner

## Context

The official v3.1.1.9 Bluetooth Controller supports idempotent init/deinit,
but its CP IPC worker discards both return values and always sends a success
status.  An AP that trusted only that reply could clear local ownership after
a failed CP teardown.  The official Wi-Fi stack has a different contract:
its full controller deinit is unsupported and the AP proxy does not close all
mailbox channels.

The NuttX Bluetooth registration API also has no usable unregister/re-register
path in this configuration.  Tuya's TAL adds cooperative stop/delete for
threads owned by its upper layer, while its low adapter and the Beken SDK still
use the native self-delete contract.  Those layers do not authorize changing
the SDK-wide `rtos_delete_thread(NULL)` meaning.

## Decision

1. Preserve the official SDK OS-thread ABI.  `rtos_delete_thread(NULL)` remains
   current-thread deletion; radio lifecycle is not implemented by changing the
   generic OS adapter.
2. CP wraps the real Bluetooth init/deinit leaves and changes a versioned
   `CP_BT_ACTIVE` RPTUN flag only after the real operation succeeds.  It keeps
   an independent, SWD-visible diagnostic record of requests, successes,
   state and last error.
3. AP treats Bluetooth init/deinit as an idempotent desired-state transaction.
   The CP-owned active flag is authoritative.  A lost reply whose committed
   flag already matches is reconciled; a reply/flag mismatch leaves AP state
   `UNKNOWN` and fails closed instead of discarding the teardown token.
4. Repeated Controller deinit/init validation runs only before the stock NuttX
   Host takes ownership.  A default-off board option supplies bounded hardware
   stress without claiming unsupported Host unregister/re-register semantics.
5. Wi-Fi remains CP/whole-chip lifetime state.  AP-only stop, restart or
   recovery returns `-EBUSY` while the Wi-Fi Controller is active; whole-chip
   reset is the supported recovery boundary until a complete vendor teardown
   exists.
6. Diagnostic records are local AP/CP evidence, not a second shared protocol.
   Runtime ownership crosses cores only through the fixed RPTUN control ABI.

## Consequences

- AP no longer trusts the SDK's unconditional Bluetooth success event as proof
  that CP ownership changed.
- A delayed or lost event cannot split an already committed Controller state,
  while an actual vendor failure remains retryable and visible.
- Bluetooth Controller churn is board-testable without racing the NuttX Host.
  Full Host/device re-registration is explicitly outside the supported API.
- Wi-Fi teardown is intentionally less dynamic than Bluetooth teardown.  This
  is a truthful SDK constraint rather than a wrapper-simulated close.

## Rejected alternatives

1. Clear AP state before CP deinit completes.  Rejected because a vendor
   failure would create two owners with contradictory state.
2. Trust the vendor IPC status event.  Rejected because v3.1.1.9 emits success
   without checking the real init/deinit result.
3. Force-delete SDK workers or change self-delete semantics.  Rejected because
   it violates the shared SDK OS ABI and bypasses subsystem cleanup.
4. Pretend Wi-Fi has a symmetric deinit.  Rejected because the pinned SDK and
   proxy channel lifecycle do not supply that contract.
