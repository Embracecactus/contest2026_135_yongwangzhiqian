# Public wake reply asset

`wake_reply_public.pcm` is a public, non-cloned ordinary synthetic reply for
the Chinese text `我在`.  It is raw mono PCM16LE at 16 kHz, 23,914 bytes, with
SHA-256 `62091092ee2cde223e43a8a5364a5769620dbb2be903801ebcdfe16975b351ae`.
It is an output artifact, not evidence of listening or device acceptance.

Regeneration input is the Apache-2.0 Kokoro-82M-v1.1-zh model card:
https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh .  The pinned public
Sherpa-ONNX conversion is
https://huggingface.co/csukuangfj/kokoro-int8-multi-lang-v1_1 at commit
`155831f1b4ba23b1f5c058be6a61df90cefb2a37`: `model.int8.onnx` SHA-256
`bda15858163726a492d02a9a727bc263551b86ac77f90812c4b30ff41d380e26` and
`voices.bin` SHA-256
`e64a5a581d8c2a350d848f51c3121657cd83aa07ed6109172177345874a7244c`.

Generate once with `sherpa-onnx==1.13.8`, CPU provider and one thread, using
that commit's `tokens.txt`, `lexicon-zh.txt`, and `espeak-ng-data`; synthesize
`我在` with `sid=0`, then deterministically linearly resample 24 kHz float
samples to signed 16-bit 16 kHz PCM.  Do not substitute a personal recording
or a different model/voice without a new provenance review.
