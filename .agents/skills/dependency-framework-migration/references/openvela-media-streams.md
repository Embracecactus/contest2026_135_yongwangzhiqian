# NuttX/OpenVela Media streaming failures

Use only with the checked-out Media, FFmpeg and audio lower-half versions.
These are debugging branches, not claims that every such backend implements
the same nonblocking or trigger behavior. Obtain device paths, format, queue
limits and deadlines from project configuration.

## Public I/O and progress

1. Read the public API **implementation**, selected transport and lower-half
   before relying on `poll`. Confirm the descriptor's nonblocking behavior.
2. For a verified nonblocking stream, attempt read/write first. Count positive
   short I/O as progress and retain the unconsumed suffix. Only on `EAGAIN`
   wait for readiness, using the remaining time on the same monotonic
   deadline. Handle interrupt, zero/EOF and permanent error separately.
3. Do not grant a new whole timeout after every short transfer. Do not treat a
   readiness event as a completed write. Conversely, an unsuitable poll path
   can produce a timeout even when a direct nonblocking write makes progress.
4. Calculate buffering from the largest producer burst, sample rate, channel
   count, sample width and consumer service latency. A queue count that fixes
   one product is not a reusable default. Bound both RAM and backpressure.

## Format, end-of-stream and teardown

- Trace negotiated sample format/rate through caller, Media options, FFmpeg
  conversion and hardware. Check option storage width against enum/field
  size; an integer option setter targeting the wrong width can corrupt nearby
  state. Removing the returned error does not fix negotiation.
- Keep producer EOF distinct from player queue drain and hardware completion.
  Closing on EOF alone can truncate speech. Conversely, waiting forever for
  new input after EOF can leave the session busy.
- On cancel or error, stop producers, unblock I/O, close/drain as the API
  contract requires, and wait for workers to become quiescent before a new
  capture. Record the first format/open error as well as any later recovery.
- A UART `MIC busy` after an apparently successful cancel is a resource
  ownership failure until proven otherwise. Do not add another capture worker.

## Trigger integration

Reuse the official trigger/frontend where actually configured. The model
adapter supplies validated tensor/model state and product trigger policy;
it must not start a competing MIC owner. Check model lifetime, arena alignment
and allocation failure. Accumulate arbitrary PCM chunks into complete frontend
frames; transport callbacks need not align with feature hops. Serialize access
to a single interpreter and clear/reset state at the documented session
boundary. Validate latch/release behavior before resuming detection after TTS.

## Evidence

Inspect the compiled source list and linked object before runtime. Use existing
logs for byte progress, phase, error and resource release; keep payloads out.
Report source, build and physical playback separately. A recovered turn does
not erase a prior format failure, clipped tail, timeout or busy microphone.
