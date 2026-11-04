#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replace the nine approved wake cues, using the film's existing DSP."""
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

import numpy as np
import soundfile as sf


def outside_hash(path, ranges):
    digest = hashlib.sha256()
    with sf.SoundFile(path) as stream:
        intervals = []
        start = 0
        for left, right in ranges:
            intervals.append((start, left))
            start = right
        intervals.append((start, len(stream)))
        for start, end in intervals:
            stream.seek(start)
            while start < end:
                count = min(262144, end - start)
                samples = stream.read(count, dtype="int32", always_2d=True)
                digest.update(samples.tobytes())
                start += len(samples)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--wake-pcm", type=Path, required=True,
                        help="Operator-authorized 16 kHz mono PCM16LE input")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--time-limit-seconds", type=int, default=1800)
    args = parser.parse_args()
    project, out = args.project.resolve(), args.output.resolve()
    if args.time_limit_seconds < 300:
        parser.error("--time-limit-seconds must be at least 300")
    deadline = time.monotonic() + args.time_limit_seconds
    out.mkdir(parents=True, exist_ok=True)
    for name in ("dialogue.wav", "mix.wav", "master.wav"):
        if (out / name).exists():
            parser.error(f"output already exists: {out / name}")
    sys.path.insert(0, str(project / "tools"))
    import build_shaniu_voice as voice
    import voice_timing
    import editlib
    import mix
    import master

    original_wake = project / "audio/voice/shaniu/W.wav"
    assert hashlib.sha256(original_wake.read_bytes()).hexdigest() == (
        "9f7fc3586157e924e7e56d68e9571c99acdd27048150f3d831d860440fb0be84"
    )
    data = args.wake_pcm.read_bytes()
    if not data or len(data) % 2:
        parser.error("wake PCM must contain complete PCM16LE samples")
    samples = voice.up48(np.frombuffer(data, dtype="<i2").astype(np.float32) / 32768)
    old, sample_rate = sf.read(original_wake, dtype="float32")
    if sample_rate != 48000 or old.ndim != 1 or not np.any(samples):
        parser.error("expected mono 48 kHz original and non-silent replacement")
    samples *= min(voice.speech_rms(old) / voice.speech_rms(samples),
                   0.89 / float(np.max(np.abs(samples))))
    new_wake = out / "board-wake.wav"
    sf.write(new_wake, samples, 48000, subtype="PCM_24")
    old_span = voice_timing.span(original_wake)
    new_span = voice_timing.span(new_wake)
    edit = editlib.load_edit()
    captions = json.loads((project / "subtitles/subtitles.json").read_text())
    placements = []
    for cue in edit["lines"]:
        if cue["id"] != "W":
            continue
        old_at = editlib.film_from_story(edit, cue["at"])
        new_at = old_at + old_span["on"] - new_span["on"]
        start, end = new_at + new_span["on"], new_at + new_span["off"]
        caption = next(c for c in captions if c["line"] == "W"
                       and abs(c["start"] - old_at - old_span["on"]) < 0.03)
        placements.append({
            "tag": cue["tag"], "old_at": old_at,
            "new_at": round(float(new_at), 6),
            "speech_start": round(float(start), 6),
            "speech_end": round(float(end), 6),
            "subtitle": [caption["start"], caption["end"]],
            "speech_inside_caption": bool(start >= caption["start"] - .021
                                           and end <= caption["end"] + .021),
            "gain_db": cue.get("gain_db", 0),
        })
    if len(placements) != 9 or not all(c["speech_inside_caption"] for c in placements):
        parser.error("replacement must fit all nine existing caption windows")
    plan = {
        "source_asset_sha256": hashlib.sha256(data).hexdigest(),
        "source_asset_bytes": len(data), "source_rate": 16000,
        "old_span": old_span, "new_span": new_span,
        "replacement_wav_sha256": hashlib.sha256(new_wake.read_bytes()).hexdigest(),
        "placements": placements,
    }
    (out / "replacement-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    if args.prepare_only:
        print("nine replacement windows prepared; original audio unchanged")
        return
    old_voice = mix.speaker(mix.read(original_wake))
    new_voice = mix.speaker(mix.read(new_wake))
    original_mix = project / "audio/stems/mix.wav"
    original_dialogue = project / "audio/stems/dialogue.wav"
    premaster, rate = sf.read(original_mix, dtype="float64", always_2d=True)
    dialogue, dialogue_rate = sf.read(original_dialogue, dtype="float64", always_2d=True)
    assert rate == dialogue_rate == 48000 and premaster.shape == dialogue.shape
    # The original producer normalizes only above .98. A lower stored peak
    # proves its additive, unnormalized path was used for this master.
    old_peak = float(np.max(np.abs(premaster)))
    assert old_peak < 0.97999, "original global normalization needs separate handling"
    ranges = []
    for cue in plan["placements"]:
        assert cue["speech_inside_caption"]
        gain = mix.db(cue["gain_db"]) * 0.85
        old_start = round(cue["old_at"] * rate)
        new_start = round(cue["new_at"] * rate)
        for buffer in (premaster, dialogue):
            buffer[old_start:old_start + len(old_voice)] -= old_voice[:, None] * gain
            buffer[new_start:new_start + len(new_voice)] += new_voice[:, None] * gain
        ranges.append((min(old_start, new_start),
                       max(old_start + len(old_voice), new_start + len(new_voice))))
    ranges.sort()
    assert len(ranges) == 9
    assert all(a[1] < b[0] for a, b in zip(ranges, ranges[1:]))
    new_peak = float(np.max(np.abs(premaster)))
    assert new_peak < 1.0, "replacement premaster would clip"
    sf.write(out / "dialogue.wav", dialogue, rate, subtype="PCM_24")
    sf.write(out / "mix.wav", premaster, rate, subtype="PCM_24")
    del dialogue
    for source, result in ((original_mix, out / "mix.wav"),
                           (original_dialogue, out / "dialogue.wav")):
        assert outside_hash(source, ranges) == outside_hash(result, ranges)
    print("nine replacements complete; all other premaster samples unchanged", flush=True)
    assert time.monotonic() < deadline - 120
    settings = json.loads((project / "audio/master/master.json").read_text())["result"]
    result = master.true_peak_limit(premaster * mix.db(settings["gain_db"]),
                                    rate, settings["ceiling_db"])
    sf.write(out / "master.wav", result.astype(np.float32), rate, subtype="PCM_24")
    del premaster, result
    loudness, true_peak, loudness_range = master.measure(out / "master.wav")
    assert -16.3 <= loudness <= -15.7, loudness
    assert true_peak <= -1.0, true_peak
    report = {
        "replacement_count": 9,
        "source_asset_sha256": plan["source_asset_sha256"],
        "sample_rate": rate,
        "frames": sf.info(out / "master.wav").frames,
        "old_premaster_peak": old_peak,
        "new_premaster_peak": new_peak,
        "other_premaster_and_dialogue_samples_identical": True,
        "captions_preserved_and_speech_inside_all_nine_cues": True,
        "master_gain_db": settings["gain_db"],
        "master_ceiling_db": settings["ceiling_db"],
        "integrated_lufs": loudness,
        "true_peak_dbtp": true_peak,
        "loudness_range_lu": loudness_range,
        "finished_unix": time.time(),
        "subjective_listening": "not_claimed",
    }
    (out / "audio-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
