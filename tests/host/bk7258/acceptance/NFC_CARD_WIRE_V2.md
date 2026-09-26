# Device-internal card samples for scene matching

Frozen software contract, 2026-09-27, S38. NFC-01/NFC-02 prerequisite;
not a completed binding, dwell detector, authorized scene or board result.

The existing endpoint and 24-byte request / 40-byte response layouts remain.
V1 commands 1/2 continue presence/HCE with all response reserved bytes zero;
existing CLI uses only V1 and must not expose UID. V2 accepts only command 3
(CARD). It is an explicit one-shot hardware operation, not a status query.
Only the trusted board CP/AP RPMsg peer may use it. A version match joins
session/sequence/connection epoch in reply correlation; V2 cannot satisfy V1.

V2 response bytes 28..39 are a 12-byte card sample: length u8, SAK u8, ten UID
bytes. Success requires present=1, length in {4,7,10}, no incomplete SAK bit,
and zero tail beyond that length. Negative RPC/operation status requires
present=0 and all twelve bytes zero. An absent successful response also has
zero payload, but the current CARD source only publishes selected cards or
errors. EAGAIN and ETIMEDOUT remain errors: neither permits a future scene
rearm as evidence of physical removal. Malformed/partial or close-failed
samples are never published. A repeated identical request uses the exact
cached result and does not touch hardware again; reconnect invalidates it.

This adds a separate internal device-data contract, not a relaxation of V1
privacy. UID is never ownership authentication. The new bytes are not added
to BLE/USB/App/debug output. Subsequent scene code must accept only an owner-
configured low-risk binding, apply power/OTA/voice admission, and use the
existing focus owner. No automatic polling, binding, persistent storage,
scene dispatch, or sampling interval is introduced in S38. Those interfaces
and L3 evidence remain explicit gaps rather than being counted as PASS here.
