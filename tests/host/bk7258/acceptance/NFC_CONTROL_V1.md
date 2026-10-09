# Authenticated NFC binding control v1 (S44)

SDC1 config kind12 uses existing authenticated BEGIN/APPEND/APPLY/READ; no new
transport, authentication or staging rules. NCF1 is40 bytes: magic4, action BE32,
slot BE32, reserved BE32=0, expected revision BE64, operation BE64, duration BE64.
Actions1 LOAD/2 ENROLL/3 REMOVE match worker jobs; action4 cancels the named job
and requires slot/revision/duration zero. BEGIN validates length only, APPLY
accepts a copied job, never proves durable completion. CONFIG_CANCEL discards
staging only; it never claims cancellation of an already accepted worker job.

NCS1 is112 bytes: magic4, phase BE32, error signed BE32, reserved BE32=0,
operation BE64, revision BE64, operation floor BE64, reserved BE64=0, eight BE64
slot durations. READ accepts offsets0,16,...96, copies cached status and is
allowed while quiescing. It contains no UID or secret. Readers must recheck the
header after collecting chunks to reject a changed operation/revision/phase.
Query is not LOAD; explicit LOAD populates bindings after startup.

Clients choose a nonzero operation greater than the returned floor for a new
job, preserving the exact ID/body for retry. The floor includes the most recent
durable operation after LOAD, preventing restart from reusing that ID. UINT64_MAX
means exhausted; do not wrap. Concurrent clients can conflict and must read back,
not overwrite. Only latest job result is cached; older lookup is not guaranteed.

An accepted job survives transport disconnect; the device completes or reports
its actual terminal result. Reconnect may read it without resubmission. This is
not a session-close cancellation promise. Product stop/reset still cancels work
before commit and drains commit. No UID authorizes access. No automatic scan or
NFC scene playback is enabled by this protocol; native UI remains a next slice.
