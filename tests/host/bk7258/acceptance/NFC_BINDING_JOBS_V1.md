# NFC binding worker jobs (S43)

This internal AP interface is callable only by the product's authorized owner
adapter. It grants no authority by itself; public BLE/USB routing is a separate
required integration. A UID never authenticates an owner. One slot in the existing
NFC worker accepts explicit LOAD, ENROLL and REMOVE tasks. No ambient scan,
thread, timer, automatic enrollment or scene dispatch is enabled by this API.

A request carries nonzero operation ID, expected binding revision, slot and
focus duration. LOAD has zero revision/slot/duration; REMOVE duration is zero.
ENROLL performs one complete card selection then a durable revisioned save.
The fixed internal root is prepared only by an explicit task, on the verified
existing parent filesystem. Status reads are cache-only, expose no UID and do
not prepare storage or scan. They include task ID, phase/error, binding revision
and eight slot durations. Invalid input has no I/O. RPMsg and local jobs cannot
concurrently own the worker. Only the latest accepted operation/result is held;
identical replay is idempotent, conflicting reuse fails. Older operation IDs in
one service lifetime fail ESTALE. IDs are monotonic for this internal adapter.

Phases: idle, pending, running, committing, succeeded, failed, canceled, unknown.
Cancellation before commit requests cancellation; active I/O still has to exit.
Only the worker publishes canceled after active cleanup. Pending cancellation
can finish immediately. After entering committing, cancellation returns EALREADY;
no claim of remote cancellation or undo. Stop closes admission, cancels pending/
precommit tasks and waits through real I/O/commit. An unknown durable result is
not failure or success and disables store reuse until reset/restart resolution.

Authorized reset may use the existing reset storage worker only after NFC is
quiescent and has no in-flight I/O. It reserves that ownership, clears the fixed
files, invalidates all binding cache/task state on success, and releases ownership
without resuming NFC. Failed reset keeps cached bindings unusable. No ordinary
query or card request may erase data. Old canceled jobs cannot revive on resume.
