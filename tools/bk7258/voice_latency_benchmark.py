#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare the production voice answer path under controlled host peers.

This benchmark compiles the real Agent loop, final SSE parser, voice sentence
worker, bounded PCM queue and Media ownership path.  Only ASR text, provider
timing, TTS PCM and the Media sink are controlled.  It records lengths and
timings, never prompts, transcripts, answer bodies, audio, or credentials.

R0: native non-streaming planning response, then TTS (historical baseline)
R1: native planning path plus Media prepare overlapped with PCM prefill
R2: R1 plus one final/tools admission stream for ordinary answers

The first_media event is a Media-write proxy.  It is not physical DAC evidence.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


TEAM_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = TEAM_ROOT.parent
TEST_ROOT = TEAM_ROOT / "tests/host/bk7258"
sys.path.insert(0, str(TEST_ROOT))

import test_shaniu_mixed_tools as fixture  # noqa: E402


RESULT_RE = re.compile(r"^LATENCY_RESULT\s+(.*)$", re.MULTILINE)
TOOL_RESULT_RE = re.compile(r"^TOOL_LATENCY_RESULT\s+(.*)$", re.MULTILINE)
RESOURCE_RE = re.compile(
    r"^RESOURCE_RESULT user_s=(\S+) system_s=(\S+) max_rss_kb=(\d+)$",
    re.MULTILINE,
)

VARIANTS = {
    "R0": {"route": False, "early_media_prepare": False},
    "R1": {"route": False, "early_media_prepare": True},
    "R2": {"route": True, "early_media_prepare": True},
}

PROFILES = {
    "normal": {
        "tail_ms": 700,
        "first_token_ms": 260,
        "tts_first_us": 30000,
        "tts_chunk_us": 10000,
        "media_open_us": 80000,
    },
    "weak": {
        "tail_ms": 1100,
        "first_token_ms": 450,
        "tts_first_us": 60000,
        "tts_chunk_us": 20000,
        "media_open_us": 120000,
    },
}

TOOL_PROFILE = {
    "route_us": 220000,
    "plan_us": 200000,
    "final_us": 260000,
    "tts_first_us": 30000,
    "tts_chunk_us": 10000,
    "media_open_us": 80000,
}


def compile_probe(variant: str, directory: Path, case: str) -> Path:
    agent = WORKSPACE / "packages/ai_agent"
    cjson = WORKSPACE / "apps/netutils/cjson/cJSON"
    directory.mkdir()
    source = directory / "agent_loop_under_test.c"
    probe = directory / "probe.c"
    binary = directory / f"voice-latency-{case}-{variant.lower()}"
    source.write_text(fixture.source_for(case), encoding="utf-8")
    probe.write_text(fixture.probe_code(case), encoding="utf-8")

    settings = VARIANTS[variant]
    command = [
        "cc",
        "-DTEST_MIXED_TTS=1",
        f"-DAI_AGENT_TTS_EARLY_MEDIA_PREPARE={int(settings['early_media_prepare'])}",
        "-std=gnu11",
        "-pthread",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-error=deprecated-declarations",
        "-Wno-error=stringop-truncation",
        "-O2",
        "-D_DEFAULT_SOURCE=1",
        "-D_GNU_SOURCE=1",
        "-DOK=0",
        "-DERROR=-1",
        "-ffunction-sections",
        "-fdata-sections",
        "-Wl,--gc-sections",
        "-I",
        str(directory),
        "-I",
        str(TEST_ROOT / "mocks"),
        "-I",
        str(agent / "include"),
        "-I",
        str(agent / "src"),
        "-I",
        str(TEAM_ROOT / "app/bk7258"),
        "-I",
        str(cjson),
        str(probe),
        str(cjson / "cJSON.c"),
        str(agent / "src/llm/llm_proxy.c"),
        str(agent / "src/llm/llm_parse.c"),
        str(agent / "src/llm/llm_stream.c"),
        str(agent / "src/core/message_bus.c"),
        str(TEAM_ROOT / "app/bk7258/bk7258_focus.c"),
        str(TEAM_ROOT / "app/bk7258/bk7258_focus_intent.c"),
        "-o",
        str(binary),
    ]
    if case == "first-answer":
        command.insert(2, "-DTEST_PLAN_LATENCY=1")
        command.insert(3, f'-DTEST_LATENCY_VARIANT="{variant.lower()}"')
    if settings["route"]:
        command.insert(1, "-DCONFIG_AI_AGENT_VOICE_ROUTE=1")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{variant} compile failed:\n{result.stderr}")
    return binary


def parse_fields(output: str, pattern: re.Pattern[str]) -> dict[str, int | str]:
    match = pattern.search(output)
    if not match:
        raise RuntimeError(f"missing LATENCY_RESULT:\n{output}")
    fields: dict[str, int | str] = {}
    for item in match.group(1).split():
        key, value = item.split("=", 1)
        fields[key] = value if key == "variant" else int(value)
    resource = RESOURCE_RE.search(output)
    if not resource:
        raise RuntimeError(f"missing RESOURCE_RESULT:\n{output}")
    fields["user_cpu_ms"] = round(float(resource.group(1)) * 1000)
    fields["system_cpu_ms"] = round(float(resource.group(2)) * 1000)
    fields["max_rss_kb"] = int(resource.group(3))
    return fields


def run_round(binary: Path, profile: dict[str, int]) -> dict[str, int | str]:
    environment = dict(os.environ)
    environment.update(
        {
            "FIRST_ANSWER_TAIL_MS": str(profile["tail_ms"]),
            "FIRST_ANSWER_TOKEN_MS": str(profile["first_token_ms"]),
            "VOICE_TEST_TTS_FIRST_US": str(profile["tts_first_us"]),
            "VOICE_TEST_TTS_CHUNK_US": str(profile["tts_chunk_us"]),
            "VOICE_TEST_MEDIA_OPEN_US": str(profile["media_open_us"]),
        }
    )
    command = [
        "/usr/bin/time",
        "-f",
        "RESOURCE_RESULT user_s=%U system_s=%S max_rss_kb=%M",
        str(binary),
    ]
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=15, env=environment
    )
    output = result.stdout + result.stderr
    if result.returncode or "CONTRACT_PASS" not in output:
        raise RuntimeError(f"probe failed ({result.returncode}):\n{output}")
    return parse_fields(output, RESULT_RE)


def run_tool_round(binary: Path) -> dict[str, int | str]:
    environment = dict(os.environ)
    environment.update(
        {
            "VOICE_TEST_TOOL_METRICS": "1",
            "VOICE_TEST_ROUTE_US": str(TOOL_PROFILE["route_us"]),
            "VOICE_TEST_PLAN_US": str(TOOL_PROFILE["plan_us"]),
            "VOICE_TEST_FINAL_US": str(TOOL_PROFILE["final_us"]),
            "VOICE_TEST_TTS_FIRST_US": str(TOOL_PROFILE["tts_first_us"]),
            "VOICE_TEST_TTS_CHUNK_US": str(TOOL_PROFILE["tts_chunk_us"]),
            "VOICE_TEST_MEDIA_OPEN_US": str(TOOL_PROFILE["media_open_us"]),
        }
    )
    result = subprocess.run(
        [
            "/usr/bin/time",
            "-f",
            "RESOURCE_RESULT user_s=%U system_s=%S max_rss_kb=%M",
            str(binary),
        ],
        capture_output=True,
        text=True,
        timeout=15,
        env=environment,
    )
    output = result.stdout + result.stderr
    if result.returncode or "CONTRACT_PASS" not in output:
        raise RuntimeError(f"tool probe failed ({result.returncode}):\n{output}")
    return parse_fields(output, TOOL_RESULT_RE)


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def summarize(samples: list[dict[str, int | str]]) -> dict[str, object]:
    metric_names = (
        "first_token_ms",
        "tts_submit_ms",
        "first_pcm_ms",
        "media_prepare_begin_ms",
        "media_prepare_end_ms",
        "first_media_ms",
        "playback_end_ms",
        "turn_ready_ms",
        "tail_release_ms",
        "user_cpu_ms",
        "system_cpu_ms",
        "max_rss_kb",
    )
    summary: dict[str, object] = {"rounds": len(samples)}
    for metric in metric_names:
        values = [
            int(sample[metric])
            for sample in samples
            if metric in sample and int(sample[metric]) >= 0
        ]
        summary[metric] = None if not values else {
            "p50": percentile(values, 0.50),
            "p90": percentile(values, 0.90),
            "p95": percentile(values, 0.95),
            "max": max(values),
        }
    invariants = ("plans", "finals", "text_bytes", "pcm_bytes")
    summary["invariants"] = {
        key: sorted({int(sample[key]) for sample in samples}) for key in invariants
    }
    return summary


def improvement(baseline: int, candidate: int) -> float:
    return round((baseline - candidate) * 100.0 / baseline, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--weak-rounds", type=int, default=10)
    parser.add_argument("--tool-rounds", type=int, default=10)
    parser.add_argument(
        "--output",
        type=Path,
        default=WORKSPACE / "out/voice-latency-round10-20261027/host-latency-results.json",
    )
    parser.add_argument(
        "--raw",
        type=Path,
        default=WORKSPACE / "out/voice-latency-round10-20261027/host-latency-raw.jsonl",
    )
    args = parser.parse_args()
    if args.rounds < 1 or args.weak_rounds < 0 or args.tool_rounds < 0:
        parser.error("round counts must be non-negative and normal rounds must be positive")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.raw.parent.mkdir(parents=True, exist_ok=True)
    all_samples: dict[str, dict[str, list[dict[str, int | str]]]] = {
        name: {} for name in VARIANTS
    }
    with tempfile.TemporaryDirectory(prefix="voice-latency-") as temporary:
        directory = Path(temporary)
        binaries = {
            name: compile_probe(name, directory / ("answer-" + name.lower()), "first-answer")
            for name in VARIANTS
        }
        tool_binaries = {
            name: compile_probe(name, directory / ("tool-" + name.lower()), "mixed-tts")
            for name in VARIANTS
        }
        with args.raw.open("w", encoding="utf-8") as raw:
            for profile_name, profile in PROFILES.items():
                rounds = args.rounds if profile_name == "normal" else args.weak_rounds
                for variant, binary in binaries.items():
                    samples = []
                    for sequence in range(rounds):
                        sample = run_round(binary, profile)
                        sample["profile"] = profile_name
                        sample["experiment"] = variant
                        sample["sequence"] = sequence + 1
                        raw.write(json.dumps(sample, ensure_ascii=False, sort_keys=True) + "\n")
                        raw.flush()
                        samples.append(sample)
                    all_samples[variant][profile_name] = samples
            for variant, binary in tool_binaries.items():
                samples = []
                for sequence in range(args.tool_rounds):
                    sample = run_tool_round(binary)
                    sample["profile"] = "tool"
                    sample["experiment"] = variant
                    sample["sequence"] = sequence + 1
                    raw.write(json.dumps(sample, ensure_ascii=False, sort_keys=True) + "\n")
                    raw.flush()
                    samples.append(sample)
                all_samples[variant]["tool"] = samples

    summaries = {
        variant: {
            profile: summarize(samples)
            for profile, samples in profiles.items()
        }
        for variant, profiles in all_samples.items()
    }
    comparisons: dict[str, object] = {}
    for profile in PROFILES:
        if not all_samples["R0"][profile]:
            continue
        r0 = summaries["R0"][profile]["first_media_ms"]
        r1 = summaries["R1"][profile]["first_media_ms"]
        r2 = summaries["R2"][profile]["first_media_ms"]
        comparisons[profile] = {
            "R1_vs_R0_first_media_reduction_pct": {
                key: improvement(int(r0[key]), int(r1[key])) for key in ("p50", "p95")
            },
            "R2_vs_R0_first_media_reduction_pct": {
                key: improvement(int(r0[key]), int(r2[key])) for key in ("p50", "p95")
            },
            "R2_vs_R1_first_media_reduction_pct": {
                key: improvement(int(r1[key]), int(r2[key])) for key in ("p50", "p95")
            },
        }
    if args.tool_rounds:
        r0 = summaries["R0"]["tool"]["first_media_ms"]
        r1 = summaries["R1"]["tool"]["first_media_ms"]
        r2 = summaries["R2"]["tool"]["first_media_ms"]
        comparisons["tool"] = {
            "R1_vs_R0_first_media_change_pct": {
                key: round((int(r1[key]) - int(r0[key])) * 100.0 / int(r0[key]), 1)
                for key in ("p50", "p95")
            },
            "R2_vs_R0_first_media_change_pct": {
                key: round((int(r2[key]) - int(r0[key])) * 100.0 / int(r0[key]), 1)
                for key in ("p50", "p95")
            },
        }

    result = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "evidence": {
            "level": "controlled_host_boundary",
            "starts_at": "T4-equivalent Agent LLM request entry with fixed ASR text",
            "first_audio_event": "first Media write completion proxy, not DAC/acoustic T10",
            "physical_T0": None,
            "physical_T10": None,
            "cloud_provider": None,
            "bk7258_hardware": None,
        },
        "profiles": PROFILES,
        "tool_profile": TOOL_PROFILE,
        "variants": VARIANTS,
        "summaries": summaries,
        "comparisons": comparisons,
        "correctness_contract": {
            "text_commits_per_turn": 1,
            "expected_text_bytes": 103,
            "expected_pcm_bytes": 16384,
            "ordered_pcm_checked_byte_for_byte": True,
            "single_media_open_per_turn": True,
        },
        "raw_samples": str(args.raw.resolve()),
    }
    args.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({"output": str(args.output), "comparisons": comparisons}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
