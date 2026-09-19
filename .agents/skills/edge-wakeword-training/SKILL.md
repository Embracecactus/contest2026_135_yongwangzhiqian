---
name: edge-wakeword-training
description: Train, quantize and integrate a first usable local wake-word model with an existing embedded runtime and training toolchain, using authorized public speech or licensed multi-voice synthesis. Use for model/runtime compatibility and independently evaluated wake detection. Not for voice cloning, speaker identity, cloud text matching, a new training platform or claims of human generalization from synthetic audio.
---

# Embedded wake-word model delivery

## Inputs and scope

Obtain the accepted wake phrase and pronunciation, existing audit/train/evaluate
entry points, runtime/frontend source, target memory budget, available compute,
and authorized asset roots. Discover public-source licenses before downloading
or synthesizing. Do not scan unrelated private directories. Existing session
authorization does not become permission for another project's data, payments,
microphone recording or deployment.

Use [the data and compatibility procedure](references/data-and-runtime.md)
throughout. This method assumes a trainable small classifier and a reachable
TFLite Micro or comparable embedded backend. It is not a ready-made model for
arbitrary languages, chipsets or frontend families.

## Execute

1. Freeze the actual contract from running code/config: PCM format/rate,
   frontend frame/hop/window and feature transform, tensor shape/order,
   label indices, operators, quantization, arena and trigger policy. Confirm
   product text, training labels and firmware configuration describe the same
   phrase; labels alone cannot change pronunciation.
2. Inventory only current dependencies and authorized asset locations. Reuse
   compatible models/frontends/tools when present. If weights are missing,
   prepare assets and train; do not repeatedly ask for a user model when
   autonomous asset preparation was authorized. Bound source search and use
   an available licensed alternative when a source fails.
3. Start with a small actual-speech dataset and the existing training entry.
   Include target positives, ordinary speech, near/incomplete phrases,
   silence and environmental negatives. Confirm mixed-language pronunciation
   from the actual synthesizer output/token path before generating a batch.
4. Split by original source and real speaker/voice identity before augmentation.
   Freeze a final held-out set before choosing candidates; use a separate
   validation set for model/policy selection. Hold out voices where possible;
   save the split and provenance. Run feature
   extraction → training → quantization → export → current runtime load once
   before scaling data or epochs. Use local compute; no new paid service or
   training platform by default.
5. Evaluate with the existing independent evaluator and the actual frontend
   and trigger policy. Report classification and continuous-stream triggering
   separately. Inspect target misses, ordinary-speech triggers and background
   triggers; a non-wake subclass confusion is different from a false wake.
6. Make a finite next improvement based on the observed failure: pronunciation,
   frontend mismatch, onset timing, split leakage, clipping/level, room effects
   or genuinely insufficient data. Confirm the loaded model hash first. Do
   not substitute random weights, alter labels to claim success, or keep
   lowering the threshold until a demonstration happens to trigger.
7. Integrate the candidate using the project's established model/metadata and
   build mechanism. Preserve one capture/trigger owner and connect the actual
   local event to the existing conversation entry. Account for model lifetime,
   aligned arena, chunk-to-frame buffering, cancel and detection rearming.
8. Build, sign and deploy only as authorized using existing project tools.
   Verify runtime model identity, load, arena usage, inference timing and
   the intended active-conversation or standby state after a real turn.
   Do not require a new wake for every utterance unless that is the product
   contract. Use legal held-out audio through an
   authorized physical speaker if available. Name the playback device before
   making sound; do not accidentally route to the operator's headphones.

## Output and acceptance

Store model, metadata, source/license manifest, frozen split/policy, training
and independent evaluation results in existing project asset/output locations.
Include model bytes/hash, input/output types and quantization, operators,
allocated/used RAM, measured latency, firmware identity, source reproduction
inputs and what was actually deployed. Keep private/raw recordings out of
shareable summaries; retain required license notices with distributed assets.

Distinguish: exported; runtime-compatible; loaded on target; file-input;
physical speaker replay; live-human speech. No independent human positives
means **human generalization unverified**, even with perfect synthetic scores.
State dataset counts/durations and overlap rather than marketing a universal
accuracy or zero false-alarm rate. A candidate file or accepted label does not
complete the product's real-wake acceptance gate.

If physical playback, credentials or a target are unavailable, finish the
independent preparation/build/integration steps and identify only the affected
gate. Continue other product work. Do not replace the product entry with PTT
or continuous cloud upload to make a local-wake result appear complete.
