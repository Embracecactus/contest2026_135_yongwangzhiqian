# Data, pronunciation and runtime contract

## Pronunciation and asset provenance

- Product spelling and synthesis spelling may differ only to preserve the
  accepted sound. Record both plus the intended pronunciation. For mixed
  languages check the selected tokenizer/phonemizer and its returned token
  IDs against the actual token table. A lexicon entry alone is not proof
  that the engine used it. Inspect a short generated sample before a batch.
- Use several licensed standard voices, including held-out voices when
  possible. Do not clone a person. Ordinary public speech is a negative
  unless the complete target phrase is actually present and verified.
- Record source URL/revision, license and any redistribution conditions.
  TTS weights, software and generated-audio terms are separate questions.
  A repository-wide label must not erase per-recording license differences.
  Stop distribution of affected assets when their permission is unresolved;
  continue with known-permitted inputs.
- Keep source ID, genuine voice/speaker ID, original waveform hash, split,
  class and transform provenance. Renaming a speaker ID does not create a
  held-out speaker. Expanded evaluations that include an earlier set are not
  two independent successes; report overlap and prior tuning exposure.

## Features and augmentation

Inspect the runtime's complete PCM-to-feature path: sample rate, channel
selection, scaling, clipping, frontend frame/hop, feature count, log/noise
normalization, history/window, stride and tensor axis order. Reuse its
implementation through existing bindings where available instead of training
on a superficially similar spectrogram.

Split original sources before deriving windows or augmentations. Derive
full-target, partial-target and silence labels from the verified original
timeline, then apply supported gain/noise/room transforms. Recomputing a VAD
boundary after lowering gain can trim the target and silently mislabel the
result. Preserve meaningful incomplete-phrase negatives. Synthetic delayed
and filtered echoes are simulated room effects, not measured room impulse
responses. Document the distinction.

Build training features in bounded batches suitable for available RAM. Use
training-only representative data for quantization. Save the seed, training
configuration, selected epoch and selection criterion. Keep a final held-out
evaluation separate from the data used to choose a candidate.

## Export and runtime checks

For an integer TFLite Micro backend, inspect every operator and version against
the actual resolver and check for unsupported floating-point fallback. Verify
input/output tensor names, shape, order, dtype, scale and zero point. Feature
conversion must obey the model's quantization contract, including clipping.
Check the output class order before applying the wake threshold.

Do not infer RAM from model file size. Verify aligned arena allocation and
**measured used bytes** on the target. Check that initialization owns the model
bytes for the interpreter lifetime and that one interpreter is serialized.
Streaming PCM arrives in arbitrary chunk lengths; buffer complete frontend
frames and reset documented state at lifecycle boundaries.

Use the same threshold, consecutive count, evaluation interval, release
threshold and cooldown in evaluation and firmware. Report score traces near
missed physical triggers: one high score followed by a low score under a
two-observation policy is a rejected candidate, not a successful wake.

## Report quality without changing its meaning

Classification: confusion matrix/counts by class and split. A silence sample
classified as ordinary non-wake speech can be correct for the binary decision
but incorrect for the multiclass classifier.

Continuous operation: true trigger count, missed complete phrases, false
triggers in ordinary/background speech, negative duration, onset/latency,
duplicate triggers and rearm behavior under the frozen runtime policy.
Zero observed false triggers over a short recording is not a demonstrated
zero long-term false-alarm rate.

If strict evaluation labels every event outside the annotated word interval as
background, retain those original miss/outside counts. Using the existing
event report and unchanged source timeline, additionally report signed delay
from annotated word end, sessions with no event and extra events per session.
A bounded late event near a target is a different observation from a trigger
in ordinary speech; time correlation alone still does not prove its cause.
Do not extend the accepted interval, change labels or call the strict check
passed to improve results. Select on validation before opening the frozen
test set; do not tune another candidate against that test result.

Physical operation: source identity, output device/route and level, approximate
geometry/environment, capture readiness, actual board trigger and downstream
turn completion. Keep file injection, physical playback and live-human results
separate. If the phrase is played while TTS owns the microphone policy, it is
not a valid idle-listening positive. An unattributed later wake is not proof
that an earlier playback succeeded.
