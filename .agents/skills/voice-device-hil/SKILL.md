---
name: voice-device-hil
description: Exercise a real embedded voice product through physical speaker-to-microphone playback, fresh camera requests, cancellation and network recovery using existing tools. Use to correlate actual trigger, capture, cloud, playback and rearm states. Not for training a model, software-injected wake acceptance, human-generalization claims or creating a new hardware harness.
---

# Physical voice-product chain

## Inputs and authority

Obtain the target identity, running firmware/model/config identity, existing
UART capture procedure, authorized playback device and legal held-out audio.
Also obtain expected product state markers, network/cloud prerequisites and
camera scope. Establish the intended service, interaction-session and
per-utterance lifetimes separately; a completed answer need not end the
interaction. Assign a single owner to UART, phone and board; use existing
reviewed tools rather than adding a probe, server or acceptance script.

Confirm what may make sound, access a camera, change networking or reboot under
this task's authorization. Announce the actual playback route and utterance
before sound; do not default to workstation headphones. A captured image is
private data unless current scope permits its handling. Preserve credentials
and configuration and keep them out of logs.

## Run a correlated physical turn

1. Confirm the artifact really running and idle-listening state from current
   device evidence. Separate declared config, packaged model and loaded model.
   Open the reviewed capture transport before triggering events. Establish
   its ready signal and live output; a buffered raw file may stay empty until
   capture closes. Retain final raw bytes as the authoritative record.
2. Verify playback output device, active speaker route and actual level with
   existing system tools. Reuse an authorized phone player if available; a
   successful host player process alone does not prove speaker output.
3. Play one held-out wake sample physically. Wait for the board's real
   accepted-wake and capture-ready transition before playing the command.
   Use bounded deadlines, not a fixed delay or an injected wake event. A
   below-policy candidate score is a miss, even if one frame scored highly.
4. Require capture/automatic close → ASR → dialogue → TTS bytes → physical
   playback/drain → per-turn resource release → the intended next state.
   In an active interaction that may be automatic capture without another
   wake phrase; rearm the hotword only when the product's exit condition is
   met. Record the earliest failed phase. HTTP success alone is not audible
   completion. Segmented checks may isolate a failure but do not replace the
   full physical interaction gate.
5. For voice-image behavior, require this request to trigger a fresh camera
   frame, followed by the matching image-tool request and answer playback.
   An old buffer, prerecorded answer or an unrelated prior snapshot does not
   satisfy the gate. Keep images out of general Skill artifacts.
6. Exercise cancellation at a specifically observed phase. Playback cancellation
   after cloud reception has finished does not establish network-request
   cancellation. Verify release and a subsequent complete turn. Never replay
   a rejected command automatically into a new session.
7. For a real network failure, use the bounded procedure in
   [network interruption](references/network-interruption.md). Correlate the
   actual active phase and interruption; if the prerequisite wake never
   occurred, mark the interruption gate not executed.
8. End the run, wait for process/port release and inspect complete logs.
   Preserve temporary setting restoration evidence. Do not subtract unrelated
   host/device clocks; align using paired event markers or report each clock.

## Interpretation and failure decisions

- Phrase during TTS/capture-disabled state: invalid idle positive; repeat only
  after verified rearm if necessary.
- Later wake with no attributable source: unattributed event, not proof of the
  earlier sample and not a confirmed false positive without source evidence.
- Candidate accepted once but rejected by consecutive-frame policy: inspect
  score/window/frontend alignment and loaded model before changing thresholds.
- Format failure followed by `MIC busy`: retain both errors even if a later
  turn works. Inspect resource/format ownership through the migration workflow.
- Two unchanged failures: confirm running bytes and earliest state transition;
  do not keep flashing the same artifact or increasing random delays.
- Acoustic playback impossible: finish nonacoustic gates and mark this gate
  unverified. Do not ask for personal recordings when they were not a task
  prerequisite; do not substitute software injection for physical acceptance.

## Deliver

Store results in the existing project evidence location: source/audio identity,
route/level, rough placement, running versions, phase evidence, miss/success
counts, errors, restoration and next gate. Explicitly label file input,
physical speaker replay and live-human speech. Synthetic playback can prove
an acoustic path but cannot establish human generalization or a universal
false-trigger rate. Complete only the phases actually exercised.
