#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Controlled R1/R2/R3 voice latency comparison.

The executable under test contains the current production Agent loop, strict
SSE/voice-envelope parser, sentence worker, bounded PCM queue and sole Media
owner. Provider, tool, TTS and Media delays are controlled. Output contains
only timing, count, size and result metadata; no prompt, transcript, tool
argument/result body, PCM, credential or endpoint is retained.

first_media is a host Media-write proxy, never DAC/acoustic evidence.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


TEAM_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = TEAM_ROOT.parent
TEST_ROOT = TEAM_ROOT / "tests/host/bk7258"
import sys

sys.path.insert(0, str(TEST_ROOT))
import test_shaniu_mixed_tools as fixture  # noqa: E402


RESULT_RE = re.compile(r"^LATENCY_RESULT\s+(.*)$", re.MULTILINE)
TOOL_RESULT_RE = re.compile(r"^TOOL_LATENCY_RESULT\s+(.*)$", re.MULTILINE)
RESOURCE_RE = re.compile(
    r"^RESOURCE_RESULT user_s=(\S+) system_s=(\S+) max_rss_kb=(\d+)$",
    re.MULTILINE,
)

VARIANTS = {
    "R1": {
        "route": False,
        "stream_tools": False,
        "early_media_prepare": True,
    },
    "R2": {
        "route": True,
        "stream_tools": False,
        "early_media_prepare": True,
    },
    "R3": {
        "route": True,
        "stream_tools": True,
        "early_media_prepare": True,
    },
}

ANSWER_PROFILES = {
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
    "tool_fragment_us": 20000,
    "final_us": 260000,
    "tts_first_us": 30000,
    "tts_chunk_us": 10000,
    "media_open_us": 80000,
}


def compile_probe(variant: str, directory: Path, case: str) -> Path:
    agent = WORKSPACE / "packages/ai_agent"
    cjson = WORKSPACE / "apps/netutils/cjson/cJSON"
    directory.mkdir(parents=True)
    source = directory / "agent_loop_under_test.c"
    probe = directory / "probe.c"
    binary = directory / f"round11-{case}-{variant.lower()}"
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
    if settings["stream_tools"]:
        command.insert(1, "-DCONFIG_AI_AGENT_VOICE_STREAM_TOOLS=1")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(
            f"{variant}/{case} compile failed:\n{result.stdout}{result.stderr}"
        )
    return binary


def parse_fields(output: str, pattern: re.Pattern[str]) -> dict[str, int | str]:
    match = pattern.search(output)
    if not match:
        raise RuntimeError(f"missing latency result:\n{output}")
    fields: dict[str, int | str] = {}
    for item in match.group(1).split():
        key, value = item.split("=", 1)
        fields[key] = value if key == "variant" else int(value)
    resource = RESOURCE_RE.search(output)
    if not resource:
        raise RuntimeError(f"missing resource result:\n{output}")
    fields["user_cpu_ms"] = round(float(resource.group(1)) * 1000)
    fields["system_cpu_ms"] = round(float(resource.group(2)) * 1000)
    fields["max_rss_kb"] = int(resource.group(3))
    return fields


def run(binary: Path, environment: dict[str, str], pattern: re.Pattern[str]):
    env = dict(os.environ)
    env.update(environment)
    result = subprocess.run(
        [
            "/usr/bin/time",
            "-f",
            "RESOURCE_RESULT user_s=%U system_s=%S max_rss_kb=%M",
            str(binary),
        ],
        capture_output=True,
        text=True,
        timeout=20,
        env=env,
    )
    output = result.stdout + result.stderr
    if result.returncode or "CONTRACT_PASS" not in output:
        raise RuntimeError(f"probe failed ({result.returncode}):\n{output}")
    return parse_fields(output, pattern)


def run_answer(binary: Path, profile: dict[str, int]):
    return run(
        binary,
        {
            "FIRST_ANSWER_TAIL_MS": str(profile["tail_ms"]),
            "FIRST_ANSWER_TOKEN_MS": str(profile["first_token_ms"]),
            "VOICE_TEST_TTS_FIRST_US": str(profile["tts_first_us"]),
            "VOICE_TEST_TTS_CHUNK_US": str(profile["tts_chunk_us"]),
            "VOICE_TEST_MEDIA_OPEN_US": str(profile["media_open_us"]),
        },
        RESULT_RE,
    )


def run_tool(binary: Path, tool_case: str):
    return run(
        binary,
        {
            "VOICE_TEST_TOOL_METRICS": "1",
            "VOICE_TEST_ROUTE_US": str(TOOL_PROFILE["route_us"]),
            "VOICE_TEST_PLAN_US": str(TOOL_PROFILE["plan_us"]),
            "VOICE_TEST_TOOL_FRAGMENT_US": str(
                TOOL_PROFILE["tool_fragment_us"]
            ),
            "VOICE_TEST_FINAL_US": str(TOOL_PROFILE["final_us"]),
            "VOICE_TEST_TTS_FIRST_US": str(TOOL_PROFILE["tts_first_us"]),
            "VOICE_TEST_TTS_CHUNK_US": str(TOOL_PROFILE["tts_chunk_us"]),
            "VOICE_TEST_MEDIA_OPEN_US": str(TOOL_PROFILE["media_open_us"]),
            "VOICE_TEST_TOOL_CASE": tool_case,
        },
        TOOL_RESULT_RE,
    )


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def summarize(samples: list[dict[str, int | str]]) -> dict[str, object]:
    excluded = {
        "variant",
        "profile",
        "experiment",
        "sequence",
    }
    metrics = sorted(
        {
            key
            for sample in samples
            for key, value in sample.items()
            if key not in excluded and isinstance(value, int)
        }
    )
    summary: dict[str, object] = {"rounds": len(samples)}
    invariant_names = {
        "routes",
        "plans",
        "streams",
        "finals",
        "tools",
        "text_bytes",
        "pcm_bytes",
        "tail_wait_ms",
        "tail_release_ms",
    }
    summary["invariants"] = {
        name: sorted({int(sample[name]) for sample in samples if name in sample})
        for name in sorted(invariant_names)
        if any(name in sample for sample in samples)
    }
    for metric in metrics:
        if metric in invariant_names:
            continue
        values = [
            int(sample[metric])
            for sample in samples
            if metric in sample and int(sample[metric]) >= 0
        ]
        summary[metric] = (
            None
            if not values
            else {
                "p50": percentile(values, 0.50),
                "p90": percentile(values, 0.90),
                "p95": percentile(values, 0.95),
                "max": max(values),
            }
        )
    return summary


def change_percent(baseline: int, candidate: int) -> float:
    return round((candidate - baseline) * 100.0 / baseline, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--normal-rounds", type=int, default=30)
    parser.add_argument("--weak-rounds", type=int, default=10)
    parser.add_argument("--tool-rounds", type=int, default=30)
    parser.add_argument("--multi-rounds", type=int, default=10)
    parser.add_argument("--failure-rounds", type=int, default=10)
    parser.add_argument("--timeout-rounds", type=int, default=10)
    parser.add_argument("--sequential-rounds", type=int, default=10)
    parser.add_argument(
        "--output",
        type=Path,
        default=WORKSPACE
        / "out/voice-latency-round11-20261027/host-latency-results.json",
    )
    parser.add_argument(
        "--raw",
        type=Path,
        default=WORKSPACE
        / "out/voice-latency-round11-20261027/host-latency-raw.jsonl",
    )
    args = parser.parse_args()
    if min(
        args.normal_rounds,
        args.weak_rounds,
        args.tool_rounds,
        args.multi_rounds,
        args.failure_rounds,
        args.timeout_rounds,
        args.sequential_rounds,
    ) < 1:
        parser.error("all round counts must be positive")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.raw.parent.mkdir(parents=True, exist_ok=True)

    samples: dict[str, dict[str, list[dict[str, int | str]]]] = {
        variant: {} for variant in VARIANTS
    }
    with tempfile.TemporaryDirectory(prefix="voice-round11-") as temporary:
        temp = Path(temporary)
        answer_bins = {
            variant: compile_probe(
                variant, temp / f"answer-{variant.lower()}", "first-answer"
            )
            for variant in VARIANTS
        }
        tool_bins = {
            variant: compile_probe(
                variant, temp / f"tool-{variant.lower()}", "mixed-tts"
            )
            for variant in VARIANTS
        }
        with args.raw.open("w", encoding="utf-8") as raw:
            for profile_name, profile in ANSWER_PROFILES.items():
                rounds = (
                    args.normal_rounds
                    if profile_name == "normal"
                    else args.weak_rounds
                )
                for variant, binary in answer_bins.items():
                    current = []
                    for sequence in range(1, rounds + 1):
                        sample = run_answer(binary, profile)
                        sample.update(
                            profile=profile_name,
                            experiment=variant,
                            variant=variant.lower(),
                            sequence=sequence,
                        )
                        raw.write(
                            json.dumps(
                                sample, ensure_ascii=False, sort_keys=True
                            )
                            + "\n"
                        )
                        raw.flush()
                        current.append(sample)
                    samples[variant][profile_name] = current
            tool_cases = {
                "tool_single": ("single", args.tool_rounds),
                "tool_multi": ("multi", args.multi_rounds),
                "tool_failure": ("failure", args.failure_rounds),
                "tool_timeout": ("timeout", args.timeout_rounds),
                "tool_sequential": ("sequential", args.sequential_rounds),
            }
            for profile_name, (tool_case, rounds) in tool_cases.items():
                for variant, binary in tool_bins.items():
                    current = []
                    for sequence in range(1, rounds + 1):
                        sample = run_tool(binary, tool_case)
                        sample.update(
                            profile=profile_name,
                            experiment=variant,
                            variant=variant.lower(),
                            sequence=sequence,
                        )
                        raw.write(
                            json.dumps(
                                sample, ensure_ascii=False, sort_keys=True
                            )
                            + "\n"
                        )
                        raw.flush()
                        current.append(sample)
                    samples[variant][profile_name] = current

    summaries = {
        variant: {
            profile: summarize(current)
            for profile, current in variant_samples.items()
        }
        for variant, variant_samples in samples.items()
    }
    comparisons = {}
    for profile in (
        "normal",
        "weak",
        "tool_single",
        "tool_multi",
        "tool_failure",
        "tool_timeout",
        "tool_sequential",
    ):
        baseline = summaries["R1"][profile]["first_media_ms"]
        comparisons[profile] = {}
        for variant in ("R2", "R3"):
            candidate = summaries[variant][profile]["first_media_ms"]
            comparisons[profile][f"{variant}_vs_R1_first_media_change_pct"] = {
                key: change_percent(int(baseline[key]), int(candidate[key]))
                for key in ("p50", "p95")
            }

    result = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "evidence": {
            "level": "controlled_host_boundary",
            "starts_at": "T4-equivalent Agent LLM entry with fixed ASR text",
            "first_audio_event": "first Media write completion proxy",
            "physical_T0": None,
            "physical_T10": None,
            "live_cloud": None,
            "bk7258_hardware": None,
        },
        "variants": VARIANTS,
        "answer_profiles": ANSWER_PROFILES,
        "tool_profile": TOOL_PROFILE,
        "summaries": summaries,
        "comparisons": comparisons,
        "correctness_contract": {
            "text_commit_per_turn": 1,
            "ordered_pcm_checked_byte_for_byte": True,
            "single_media_open_per_turn": True,
            "tool_arguments_and_results_not_spoken": True,
            "private_payloads_recorded": False,
        },
        "raw_samples": str(args.raw.resolve()),
    }
    args.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {"output": str(args.output), "comparisons": comparisons},
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
