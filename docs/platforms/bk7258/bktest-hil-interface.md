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

## Factory diagnostics profile

The factory engineering image may select `CONFIG_BK7258_FACTORY_DIAGNOSTICS` on
both cores.  The option defaults to `n` and is not selected by normal product
profiles.  It extends the existing physical CH340 `bkprov-v1` operator channel;
it does not add a USB protocol, public network listener, owner credential, or
persisted PC grant.

After the formal factory journal is READY, native BPI2 identity generation has
finished, and no owner control key is bound, the operator may send one `BKD1`
record with a random client ID and random 32-byte principal.  Console echo is
disabled while that secret is read.  The AP retains the record only in RAM,
publishes only `BKPC_CAP_DIAGNOSTICS`, and returns the public SHA-256 of the
device leaf certificate through the same bounded CP/AP RPC.  The host compares
that fingerprint with the certificate negotiated on the native USB TLS link
before it creates a CurrentUser-DPAPI `.spc` profile.  A TLS certificate learned
from USB alone is not sufficient.

The transient source has a fixed ten-minute monotonic deadline.  Expiry,
explicit factory-channel revocation, loss of factory/unclaimed eligibility,
owner/control binding, or reboot zeroizes the principal and invalidates the USB
lease.  It cannot be re-enabled in the same boot after expiry or revocation.
Reboot contains no credential; another explicit physical factory enrollment is
required.  No `PCG1`, owner, Wi-Fi/cloud configuration, factory identity, or
external-SD byte is written.  The diagnostics-only lease may read public
STATUS/INFO and reach engineering kinds; OTA, resource, task, scene, reset, and
ordinary configuration mutations remain denied by the existing PC guard.

## Fixed audio lifecycle operation

Engineering configuration kind 20 carries `BKA1`/`BAS1` behind the same TLS,
SDC1, diagnostics capability, and build gate.  It accepts one bounded `run`
operation and no path, URL, arbitrary PCM, sample format, volume, shell text, or
device name.  The product adapter generates the fixed low-amplitude 16-kHz mono
PCM already used by the Agent playback validation and executes three finite
sessions through the deployed `audio_playback` and Media owner: normal EOF and
drain, cancel with post-cancel write/drain rejection, and a fresh following EOF
session.  Each drain has the existing 2000-ms bound.

`BAS1` reports only session/sequence, terminal state, accepted byte counts, and
per-stage return codes.  It cannot claim acoustic output, DAC/I2S quality,
absence of xrun/noise, cloud parser behavior, or user-perceived sound.  Those
remain separate physical/online evidence.

## Voice timing report from an existing capture

The existing HIL CLI can aggregate a single boot's `[voice-stage]` records:

```sh
python3 tools/bk7258/bk7258.py hil-test voice-report \
  --from-log serial.raw --boot-id capture-001 \
  --git-sha "$AGENT_SHA" --output-dir voice-report-001
```

This offline operation opens no port and issues no cloud request. It writes
`voice_latency.json`, `turn_latency.csv`, and `SHA256SUMS.txt` into a new directory;
an existing directory is rejected. The declared source SHA is caller metadata,
not proof of the firmware currently running. Preserve the firmware identity and
model/network/sample identities alongside the original capture.

`--input-marks annotations.json` accepts an array with `request_id`, `input_mode`
(`wav`, `text`, or `acoustic`), `input_end_mono_ms`, `clock_id` (`ap:<boot-id>`),
`uncertainty_ms`, and `input_evidence`. The input evidence must identify an
annotated sample end or observed speech end aligned to the AP monotonic clock.
The uncertainty is the bound on that alignment, in milliseconds. Host receive
timestamps, the ASR final timestamp, and the detector's silence tail are not
substitutes for this origin. Text input is labelled `text-submit` and does not
produce a speech-stop latency.

An independently verified effective-answer onset may additionally supply
`effective_audio_mono_ms` and `audio_evidence` in the same clock. Only this
annotation produces `total_ms`; the annotator must distinguish answer content
from acknowledgement tones, silence, drafts, or unrelated sound. It is not an
automatic semantic/acoustic acceptance result. `turn_complete_ms` is a separate
lifecycle observation. Streaming ASR may start before input end, so a negative
ASR-start offset is valid.

Unknown observations remain `null` (empty CSV cells). The report preserves
retries, cancellation, per-request events, and queue occupancy on failed turns.
It rejects malformed traces, reused terminal request identities, duplicate
annotations, and mismatched clocks. Untagged TTS receive gaps/rebuffer logs are
not assigned to a turn or promoted to network jitter/DAC underrun counts.

The report command does not execute a turn. Explicit `input_pcm` and
`input_text` ledger events can supply the aligned origin for injected inputs.
PCM submission is labelled `injected-pcm-ready`; its `total_ms` stays null
because capture and endpoint detection were bypassed.

## Live voice benchmark (development image)

The same native USB BKTEST connection now accepts kind 24, BVB1, through the
existing diagnostics grant. Standard builds reject it. The existing product
service task consumes the upload and calls the Agent channel's input API;
the Agent retains cancellation, tools, final-body streaming, TTS and Media.
Use the matched measurement Agent source with the development firmware.

```bash
python3 tools/bk7258/bk7258.py bk_test voice benchmark \
  --port COM16 --profile /private/engineering.spc \
  --cloud-config /private/voice-benchmark.json \
  --wav /test/speech.wav --repeat 10 \
  --git-sha "$SOURCE_SHA" --output-dir "$NEW_RUN_DIR"
```

Use `--text` or `--dialogue` instead of `--wav`. Dialogue JSON is an array of
one-key objects: `{"text":"fixed input"}` or `{"wav":"relative.wav"}`.
Inputs are bounded to ten turns, 4096 UTF-8 text bytes or 15 seconds of PCM16,
16 kHz mono per turn. No resampling, truncation or speech-speed adjustment is
performed. `--cancel-after-ms` requests cancellation after RUN acknowledgement;
it is host scheduling time, not an AP stage timestamp. Lost acknowledgements
never trigger command replay.

Development cloud JSON contains `base_url`, `dialect` (MiMo: 2), `asr_model`,
`chat_model`, `tts_model`, `key_file` and `ca_der_file`. JSON and key files must
be private. The tool sends a bounded BVC2 envelope containing existing CCF1
configuration and a public CA DER certificate inside pinned TLS. The product
uses its existing device-owned identity and BVC1 validator. This can configure
only a device without an installed active cloud/owner selection; no storage
or identity is changed. Disconnect cancels the input, then clears the volatile
cloud selection after the official channel releases its resources.

`benchmark.json` records input hashes, firmware readback and terminal receipts.
Capture UART concurrently with the existing HIL transport and run
`hil-test voice-report` on the completed raw capture to produce the AP latency
ledger and CSV. A successful terminal receipt does not prove acoustic onset,
endpoint timing or DAC continuity. Measurement firmware deployment and actual
ten-turn execution must be evidenced separately from host tests.

Add `--audio-trace` on a matching engineering image to save
`audio_stream_benchmark.json` and `audio_gap_report.json`. The latter records
wait overlap within the maximum PCM gap, FIFO byte-to-Media associations and
candidate layers. Its 80% wait-coverage label is not a proven root cause;
the latest prior TLS read is not encoded-byte provenance.
The existing authenticated kind 24 read path
returns a sealed per-turn trace after playback; it does not print every PCM
callback on the timing-critical UART path. `--audio-baseline <report.json>`
adds numeric differences only for matching input hashes and complete traces.
Network conditions, generated answer length and model identity still need
review before interpreting a difference as a firmware improvement.

`--audio-status` reads only the independent sealed USB rearm header after each
turn. It supports dialogue-state checks without bulk trace traffic and does not
claim per-chunk coverage. Gap reports separate positive-duration empty waits
followed by consumption from terminal empty waits; raw counts remain available.
Byte totals also show whether every observed queued byte was submitted to Media.

The BVS1 status remains 64 bytes. Trace offset `256 + page` selects a 16-byte
page in BVA2, whose 64-byte big-endian header contains magic, version 2,
request ID high/low words, record count, dropped count, rearm state
(0 unobserved, 1 unsuccessful, 2 armed), rearm attempts, rearm monotonic ms,
sealed flag and six zero words. Offset 256 freezes the header snapshot;
records are immutable after the terminal receipt and until the next input.
The client keeps one acknowledged page request outstanding and finishes
readback before submitting another input. Interrupted exports retain their
header and completed batches of records with `readback_complete=false`.
The host adds a unique run/turn UID while preserving the device request ID.
The shared control owner must keep servicing its existing TLS deadlines while
display work runs. Claim-window opening retries when the rendering mutex is
busy; it still succeeds only after the QR reaches both screens. Engineering
loop-span logs include scheduling delay and must not be called mutex wait time.
Enrollment ignores complete independent NuttX log lines, while interleaved or
malformed diagnostic receipts remain failures. Raw UART evidence is retained.
Trace records
contain six uint32 words: sequence, kind, begin ms, end ms, bytes/value and
queue level/error. Kinds 1..8 are TTS submit, TLS receive, decoded PCM callback,
PCM queue callback, Media write, empty-queue wait, TTS response and queue
summary. TTS submit/response values are sentence ordinals; summary values are
maximum ready-consumer gap and peak queued bytes. TLS/decoder/response error
fields contain positive errno values, not queue levels.

Sealed trace reads after a successful turn permit four additional protocol
steps for response queue/flush and the next header/payload. The owner executes
at most eight steps per normal 20-ms scheduling cycle; a new input clears the
allowance. This bounds export overhead without depending on sub-tick sleeps,
changing live audio cadence or extending the diagnostic authorization TTL.

Engineering telemetry allocates at most 8192 × 24 bytes (192 KiB) per active
benchmark, independently of the unchanged 64-KiB PCM ring. It retains the
completed trace until the next input. Allocation failure, overflow and times
beyond the uint32 monotonic range increment the dropped count; such a report
cannot establish a maximum or zero underflow. Queue starvation is counted
only when a started consumer waits on an empty queue during an unfinished
TTS request. It is not a DAC underrun counter. TLS receive intervals cannot
separate server generation from network delay; callback duration can include
local queue backpressure. Rearm readback adds independent observation, without
repairing or silently reconstructing damaged historical UART records.
