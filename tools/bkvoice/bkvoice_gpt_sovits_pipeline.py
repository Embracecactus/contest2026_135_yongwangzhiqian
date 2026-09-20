#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run an audited, private GPT-SoVITS V2Pro preprocessing/training pipeline.

Third-party tools may echo transcripts on failure.  Their complete output is
therefore captured under the private run directory; this driver prints only
stage status, aggregate counts, and artifact hashes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import wave
from datetime import datetime, timezone
from pathlib import Path

import yaml


TOOL_VERSION = "1.1.0"
FORMAT = "bkvoice-gpt-sovits-v2pro-run-v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_json(path: Path, document: dict[str, object]) -> None:
    payload = json.dumps(document, sort_keys=True, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(payload)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def line_count(path: Path) -> int:
    return sum(
        bool(line.strip()) for line in path.read_text(encoding="utf-8").splitlines()
    )


def selected_names(path: Path) -> list[str]:
    names = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("|", 3)
        if len(fields) != 4 or not fields[3].strip():
            raise ValueError(f"invalid selected list row {number}")
        name = Path(fields[0]).name
        if not name or name in names:
            raise ValueError(f"duplicate selected audio basename on row {number}")
        names.append(name)
    if not names:
        raise ValueError("selected list is empty")
    return names


def created_utc() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def load_context(args: argparse.Namespace):
    root = args.gpt_sovits_root.resolve()
    # Keep the venv launcher path itself.  Path.resolve() follows its symlink to
    # /usr/bin/python and silently drops the virtual environment.
    python = args.python.absolute()
    selected = args.selected_list.resolve()
    raw_dir = args.raw_dir.resolve()
    run = args.run_dir.resolve()
    worklog_path = args.worklog.resolve()
    selection_path = args.selection_audit.resolve()
    required = (
        root / "GPT_SoVITS/configs/s2v2Pro.json",
        root / "GPT_SoVITS/configs/s1longer-v2.yaml",
        root / "GPT_SoVITS/pretrained_models/v2Pro/s2Gv2Pro.pth",
        root / "GPT_SoVITS/pretrained_models/v2Pro/s2Dv2Pro.pth",
        root / "GPT_SoVITS/pretrained_models/s1v3.ckpt",
        root / "GPT_SoVITS/pretrained_models/chinese-roberta-wwm-ext-large",
        root / "GPT_SoVITS/pretrained_models/chinese-hubert-base",
        root / "GPT_SoVITS/pretrained_models/sv/pretrained_eres2netv2w24s4ep4.ckpt",
    )
    if (
        not root.is_dir()
        or not python.is_file()
        or not selected.is_file()
        or not raw_dir.is_dir()
    ):
        raise SystemExit(
            "GPT-SoVITS root, Python, selected list, or raw directory is missing"
        )
    if any(not path.exists() for path in required):
        raise SystemExit("one or more required V2Pro pretrained assets are missing")
    worklog = json.loads(worklog_path.read_text(encoding="utf-8"))
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    if worklog.get("consent", {}).get("revoked") is not False:
        raise SystemExit("consent is revoked or missing")
    if worklog.get("stages", {}).get("speaker_verification") != "PASS":
        raise SystemExit("speaker verification has not passed")
    if selection.get("status") != "PASS":
        raise SystemExit("speaker selection audit has not passed")
    if selection.get("private_artifact_sha256", {}).get("train") != sha256_file(
        selected
    ):
        raise SystemExit("selected training list hash does not match its audit")
    names = selected_names(selected)
    if any(not (raw_dir / name).is_file() for name in names):
        raise SystemExit("selected list references missing audio")
    run.mkdir(parents=True, exist_ok=True)
    return root, python, selected, raw_dir, run, worklog_path, worklog, names


def child_environment(
    root: Path, selected: Path, raw_dir: Path, run: Path, cache_dir: Path
) -> dict[str, str]:
    env = os.environ.copy()
    python_path = [str(root), str(root / "GPT_SoVITS")]
    if env.get("PYTHONPATH"):
        python_path.append(env["PYTHONPATH"])
    env.update(
        {
            "inp_text": str(selected),
            "inp_wav_dir": str(raw_dir),
            "exp_name": run.name,
            "opt_dir": str(run),
            "i_part": "0",
            "all_parts": "1",
            "_CUDA_VISIBLE_DEVICES": "0",
            "is_half": "True",
            "version": "v2Pro",
            "bert_pretrained_dir": str(
                root / "GPT_SoVITS/pretrained_models/chinese-roberta-wwm-ext-large"
            ),
            "bert_path": str(
                root / "GPT_SoVITS/pretrained_models/chinese-roberta-wwm-ext-large"
            ),
            "cnhubert_base_dir": str(
                root / "GPT_SoVITS/pretrained_models/chinese-hubert-base"
            ),
            "sv_path": str(
                root
                / "GPT_SoVITS/pretrained_models/sv/pretrained_eres2netv2w24s4ep4.ckpt"
            ),
            "pretrained_s2G": str(
                root / "GPT_SoVITS/pretrained_models/v2Pro/s2Gv2Pro.pth"
            ),
            "s2config_path": str(root / "GPT_SoVITS/configs/s2v2Pro.json"),
            "MPLCONFIGDIR": str(cache_dir / "matplotlib"),
            "NUMBA_CACHE_DIR": str(cache_dir / "numba"),
            "HF_HOME": str(cache_dir / "huggingface"),
            "MODELSCOPE_CACHE": str(cache_dir / "modelscope"),
            "TOKENIZERS_PARALLELISM": "false",
            "PYTHONPATH": os.pathsep.join(python_path),
        }
    )
    for directory in (
        cache_dir / "matplotlib",
        cache_dir / "numba",
        cache_dir / "huggingface",
        cache_dir / "modelscope",
    ):
        directory.mkdir(parents=True, exist_ok=True)
    return env


def run_private_stage(
    name: str, python: Path, root: Path, script: str, env: dict[str, str], run: Path
) -> dict[str, object]:
    log_path = run / "logs" / f"{name}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        # This isolated venv intentionally inherits the already validated
        # user-site CUDA torch build.  Python -s would hide that package and
        # make the child appear CPU-only / torch-less.
        result = subprocess.run(
            [str(python), script],
            cwd=root,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
            text=True,
        )
    record = {
        "stage": name,
        "returncode": result.returncode,
        "log_sha256": sha256_file(log_path),
    }
    if result.returncode != 0:
        print("BKVOICE_GSV_STAGE_FAIL " + json.dumps(record, sort_keys=True))
        raise RuntimeError(f"{name} failed; inspect private log")
    print("BKVOICE_GSV_STAGE_PASS " + json.dumps(record, sort_keys=True))
    return record


def validate_name_set(directory: Path, suffix: str, names: list[str]) -> None:
    actual = {
        path.name[: -len(suffix)] if suffix else path.name
        for path in directory.glob(f"*{suffix}")
        if path.is_file()
    }
    if actual != set(names):
        raise RuntimeError(
            f"{directory.name} artifact count/name mismatch: expected={len(names)} actual={len(actual)}"
        )


def preprocess(args: argparse.Namespace) -> int:
    root, python, selected, raw_dir, run, worklog_path, worklog, names = load_context(
        args
    )
    env = child_environment(root, selected, raw_dir, run, args.cache_dir.resolve())
    records = []

    text_final = run / "2-name2text.txt"
    if (
        not args.resume
        or not text_final.is_file()
        or line_count(text_final) != len(names)
    ):
        shard = run / "2-name2text-0.txt"
        shard.unlink(missing_ok=True)
        records.append(
            run_private_stage(
                "1a-text",
                python,
                root,
                "GPT_SoVITS/prepare_datasets/1-get-text.py",
                env,
                run,
            )
        )
        if not shard.is_file() or line_count(shard) != len(names):
            raise RuntimeError("1A output count does not match selected list")
        text_final.write_text(shard.read_text(encoding="utf-8"), encoding="utf-8")

    hubert_dir = run / "4-cnhubert"
    wav32_dir = run / "5-wav32k"
    try:
        validate_name_set(hubert_dir, ".pt", names)
        validate_name_set(wav32_dir, "", names)
        hubert_ready = True
    except RuntimeError:
        hubert_ready = False
    if not args.resume or not hubert_ready:
        records.append(
            run_private_stage(
                "1b-hubert",
                python,
                root,
                "GPT_SoVITS/prepare_datasets/2-get-hubert-wav32k.py",
                env,
                run,
            )
        )
        validate_name_set(hubert_dir, ".pt", names)
        validate_name_set(wav32_dir, "", names)

    sv_dir = run / "7-sv_cn"
    try:
        validate_name_set(sv_dir, ".pt", names)
        sv_ready = True
    except RuntimeError:
        sv_ready = False
    if not args.resume or not sv_ready:
        records.append(
            run_private_stage(
                "1b-sv",
                python,
                root,
                "GPT_SoVITS/prepare_datasets/2-get-sv.py",
                env,
                run,
            )
        )
        validate_name_set(sv_dir, ".pt", names)

    semantic_final = run / "6-name2semantic.tsv"
    if (
        not args.resume
        or not semantic_final.is_file()
        or line_count(semantic_final) != len(names) + 1
    ):
        shard = run / "6-name2semantic-0.tsv"
        shard.unlink(missing_ok=True)
        records.append(
            run_private_stage(
                "1c-semantic",
                python,
                root,
                "GPT_SoVITS/prepare_datasets/3-get-semantic.py",
                env,
                run,
            )
        )
        if not shard.is_file() or line_count(shard) != len(names):
            raise RuntimeError("1C output count does not match selected list")
        semantic_final.write_text(
            "item_name\tsemantic_audio\n"
            + shard.read_text(encoding="utf-8").rstrip("\n")
            + "\n",
            encoding="utf-8",
        )

    audit = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created_utc(),
        "phase": "preprocess",
        "selected_list_sha256": sha256_file(selected),
        "utterances": len(names),
        "stage_runs": records,
        "artifacts": {
            "phoneme_sha256": sha256_file(text_final),
            "semantic_sha256": sha256_file(semantic_final),
            "hubert_count": len(list(hubert_dir.glob("*.pt"))),
            "wav32k_count": len(list(wav32_dir.iterdir())),
            "speaker_embedding_count": len(list(sv_dir.glob("*.pt"))),
        },
        "status": "PASS",
    }
    audit_path = run / "preprocess-audit.json"
    atomic_json(audit_path, audit)
    worklog["status"] = "FEATURES_PREPARED"
    worklog["stages"]["feature_preprocessing"] = "PASS"
    for candidate in worklog.get("model_candidates", []):
        if candidate.get("id") == "gpt-sovits-finetune":
            candidate["status"] = "READY_TO_TRAIN"
    worklog["events"].append(
        {
            "created_utc": audit["created_utc"],
            "event": "gpt_sovits_features_prepared",
            "status": "PASS",
            "audit_sha256": sha256_file(audit_path),
            "utterances": len(names),
        }
    )
    atomic_json(worklog_path, worklog)
    print(
        "BKVOICE_GSV_PREPROCESS_PASS " + json.dumps(audit["artifacts"], sort_keys=True)
    )
    return 0


def configure(args: argparse.Namespace) -> int:
    root, _python, _selected, _raw_dir, run, worklog_path, worklog, names = (
        load_context(args)
    )
    if line_count(run / "2-name2text.txt") != len(names):
        raise SystemExit("preprocessed phoneme artifact is incomplete")
    if line_count(run / "6-name2semantic.tsv") != len(names) + 1:
        raise SystemExit("preprocessed semantic artifact is incomplete")

    weights = run / "weights"
    s2_weights = weights / "s2"
    s1_weights = weights / "s1"
    s2_weights.mkdir(parents=True, exist_ok=True)
    s1_weights.mkdir(parents=True, exist_ok=True)
    (run / "logs_s2_v2Pro").mkdir(parents=True, exist_ok=True)
    (run / "logs_s1_v2Pro").mkdir(parents=True, exist_ok=True)
    s2 = json.loads(
        (root / "GPT_SoVITS/configs/s2v2Pro.json").read_text(encoding="utf-8")
    )
    s2["train"].update(
        {
            "batch_size": args.batch_size,
            "epochs": args.s2_epochs,
            "fp16_run": True,
            "pretrained_s2G": str(
                root / "GPT_SoVITS/pretrained_models/v2Pro/s2Gv2Pro.pth"
            ),
            "pretrained_s2D": str(
                root / "GPT_SoVITS/pretrained_models/v2Pro/s2Dv2Pro.pth"
            ),
            "if_save_latest": True,
            "if_save_every_weights": True,
            "save_every_epoch": 1,
            "gpu_numbers": "0",
            "grad_ckpt": False,
            "lora_rank": 32,
        }
    )
    s2["model"]["version"] = "v2Pro"
    s2["data"]["exp_dir"] = str(run)
    s2["s2_ckpt_dir"] = str(run)
    s2["save_weight_dir"] = str(s2_weights)
    s2["name"] = args.experiment_name
    s2["version"] = "v2Pro"
    s2_path = run / "s2-smoke.json"
    atomic_json(s2_path, s2)

    s1 = yaml.safe_load(
        (root / "GPT_SoVITS/configs/s1longer-v2.yaml").read_text(encoding="utf-8")
    )
    s1["train"].update(
        {
            "batch_size": args.batch_size,
            "epochs": args.s1_epochs,
            "precision": "16-mixed",
            "save_every_n_epoch": 1,
            "if_save_every_weights": True,
            "if_save_latest": True,
            "if_dpo": False,
            "half_weights_save_dir": str(s1_weights),
            "exp_name": args.experiment_name,
        }
    )
    s1["pretrained_s1"] = str(root / "GPT_SoVITS/pretrained_models/s1v3.ckpt")
    s1["train_semantic_path"] = str(run / "6-name2semantic.tsv")
    s1["train_phoneme_path"] = str(run / "2-name2text.txt")
    s1["output_dir"] = str(run / "logs_s1_v2Pro")
    s1_path = run / "s1-smoke.yaml"
    s1_path.write_text(yaml.safe_dump(s1, sort_keys=False), encoding="utf-8")

    audit = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created_utc(),
        "phase": "configure",
        "profile": "v2Pro-fp16-single-gpu-smoke",
        "batch_size": args.batch_size,
        "s2_epochs": args.s2_epochs,
        "s1_epochs": args.s1_epochs,
        "seed": 1234,
        "config_sha256": {"s2": sha256_file(s2_path), "s1": sha256_file(s1_path)},
        "status": "PASS",
    }
    audit_path = run / "configure-audit.json"
    atomic_json(audit_path, audit)
    worklog["status"] = "TRAINING_CONFIGURED"
    worklog["stages"]["training_configuration"] = "PASS"
    worklog["events"].append(
        {
            "created_utc": audit["created_utc"],
            "event": "gpt_sovits_training_configured",
            "status": "PASS",
            "audit_sha256": sha256_file(audit_path),
            "profile": audit["profile"],
        }
    )
    atomic_json(worklog_path, worklog)
    print(
        "BKVOICE_GSV_CONFIGURE_PASS "
        + json.dumps(audit["config_sha256"], sort_keys=True)
    )
    return 0


def run_training(args: argparse.Namespace, phase: str) -> int:
    root, python, _selected, _raw_dir, run, worklog_path, worklog, _names = (
        load_context(args)
    )
    config = run / ("s2-smoke.json" if phase == "s2" else "s1-smoke.yaml")
    if not config.is_file():
        raise SystemExit("training config is missing; run configure first")
    native_checkpoint_dir = run / (
        "logs_s2_v2Pro" if phase == "s2" else "logs_s1_v2Pro"
    )
    native_checkpoint_dir.mkdir(parents=True, exist_ok=True)
    log_dir = run / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)

    legacy_audit = run / f"train-{phase}-audit.json"
    legacy_log = log_dir / f"train-{phase}.log"
    if (
        legacy_audit.is_file()
        and not (run / f"train-{phase}-attempt-001-audit.json").exists()
    ):
        archived_audit = run / f"train-{phase}-attempt-001-audit.json"
        shutil.copy2(legacy_audit, archived_audit)
        if legacy_log.is_file():
            shutil.copy2(legacy_log, log_dir / f"train-{phase}-attempt-001.log")
        archived_record = json.loads(archived_audit.read_text(encoding="utf-8"))
        worklog["events"].append(
            {
                "created_utc": archived_record.get("created_utc", created_utc()),
                "event": f"gpt_sovits_{phase}_smoke_trained",
                "status": archived_record.get("status", "UNKNOWN"),
                "attempt": 1,
                "audit_sha256": sha256_file(archived_audit),
            }
        )
        atomic_json(worklog_path, worklog)

    attempt = 1
    while (run / f"train-{phase}-attempt-{attempt:03d}-audit.json").exists():
        attempt += 1
    log_path = log_dir / f"train-{phase}-attempt-{attempt:03d}.log"
    env = os.environ.copy()
    python_path = [str(root), str(root / "GPT_SoVITS")]
    if env.get("PYTHONPATH"):
        python_path.append(env["PYTHONPATH"])
    env.update(
        {
            "_CUDA_VISIBLE_DEVICES": "0",
            "MPLCONFIGDIR": str((args.cache_dir / "matplotlib").resolve()),
            "NUMBA_CACHE_DIR": str((args.cache_dir / "numba").resolve()),
            "TOKENIZERS_PARALLELISM": "false",
            "PYTHONPATH": os.pathsep.join(python_path),
        }
    )
    command = (
        [str(python), "GPT_SoVITS/s2_train.py", "--config", str(config)]
        if phase == "s2"
        else [str(python), "GPT_SoVITS/s1_train.py", "--config_file", str(config)]
    )
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            command,
            cwd=root,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
            text=True,
        )
    patterns = (
        (run / "weights/s2", "*.pth")
        if phase == "s2"
        else (run / "weights/s1", "*.ckpt")
    )
    checkpoints = sorted(patterns[0].glob(patterns[1]))
    record = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created_utc(),
        "phase": f"train-{phase}",
        "attempt": attempt,
        "returncode": result.returncode,
        "config_sha256": sha256_file(config),
        "log_sha256": sha256_file(log_path),
        "checkpoints": [
            {
                "name": path.name,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
            for path in checkpoints
        ],
        "status": "PASS" if result.returncode == 0 and checkpoints else "FAIL",
    }
    audit_path = run / f"train-{phase}-attempt-{attempt:03d}-audit.json"
    atomic_json(audit_path, record)
    atomic_json(legacy_audit, record)
    if record["status"] != "PASS":
        worklog["stages"][f"finetune_{phase}_smoke"] = "FAIL"
        worklog["events"].append(
            {
                "created_utc": record["created_utc"],
                "event": f"gpt_sovits_{phase}_smoke_trained",
                "status": "FAIL",
                "attempt": attempt,
                "audit_sha256": sha256_file(audit_path),
            }
        )
        atomic_json(worklog_path, worklog)
        print(
            "BKVOICE_GSV_TRAIN_FAIL "
            + json.dumps(
                {
                    "phase": phase,
                    "attempt": attempt,
                    "returncode": result.returncode,
                    "log_sha256": record["log_sha256"],
                },
                sort_keys=True,
            )
        )
        return 2
    worklog["stages"][f"finetune_{phase}_smoke"] = "PASS"
    worklog["events"].append(
        {
            "created_utc": record["created_utc"],
            "event": f"gpt_sovits_{phase}_smoke_trained",
            "status": "PASS",
            "attempt": attempt,
            "audit_sha256": sha256_file(audit_path),
            "checkpoint_count": len(checkpoints),
        }
    )
    both = all(
        worklog["stages"].get(f"finetune_{item}_smoke") == "PASS"
        for item in ("s2", "s1")
    )
    if both:
        worklog["status"] = "FINETUNE_SMOKE_COMPLETE"
        worklog["stages"]["finetune"] = "SMOKE_PASS"
        for candidate in worklog.get("model_candidates", []):
            if candidate.get("id") == "gpt-sovits-finetune":
                candidate["status"] = "SMOKE_CHECKPOINT_READY"
    atomic_json(worklog_path, worklog)
    print(
        "BKVOICE_GSV_TRAIN_PASS "
        + json.dumps({"phase": phase, "checkpoints": len(checkpoints)}, sort_keys=True)
    )
    return 0


def wave_properties(path: Path) -> dict[str, object]:
    with wave.open(str(path), "rb") as source:
        frames = source.getnframes()
        rate = source.getframerate()
        return {
            "channels": source.getnchannels(),
            "sample_width": source.getsampwidth(),
            "sample_rate": rate,
            "frames": frames,
            "duration_seconds": frames / rate,
        }


def select_reference(eval_list: Path, raw_dir: Path) -> tuple[Path, str, str, float]:
    candidates = []
    for number, line in enumerate(
        eval_list.read_text(encoding="utf-8").splitlines(), 1
    ):
        fields = line.split("|", 3)
        if len(fields) != 4 or not fields[3].strip():
            raise ValueError(f"invalid evaluation list row {number}")
        audio = raw_dir / Path(fields[0]).name
        if not audio.is_file():
            raise ValueError(f"evaluation audio is missing on row {number}")
        properties = wave_properties(audio)
        duration = float(properties["duration_seconds"])
        if 3.0 <= duration <= 10.0:
            candidates.append(
                (
                    abs(duration - 6.0),
                    number,
                    audio,
                    fields[2].strip().lower(),
                    fields[3].strip(),
                    duration,
                )
            )
    if not candidates:
        raise ValueError("evaluation list has no 3-10 second reference clip")
    _score, _number, audio, language, prompt, duration = min(candidates)
    language = {"zh": "zh", "cn": "zh", "chinese": "zh"}.get(language, language)
    return audio, language, prompt, duration


def checkpoint_from_audit(run: Path, phase: str) -> tuple[Path, dict[str, object]]:
    audit = json.loads((run / f"train-{phase}-audit.json").read_text(encoding="utf-8"))
    checkpoints = audit.get("checkpoints", [])
    if audit.get("status") != "PASS" or not checkpoints:
        raise RuntimeError(f"train-{phase} checkpoint audit has not passed")
    record = checkpoints[-1]
    suffix = ".pth" if phase == "s2" else ".ckpt"
    checkpoint = run / "weights" / phase / str(record["name"])
    if checkpoint.suffix != suffix or not checkpoint.is_file():
        raise RuntimeError(f"train-{phase} checkpoint is missing")
    if (
        checkpoint.stat().st_size != int(record["size"])
        or sha256_file(checkpoint) != record["sha256"]
    ):
        raise RuntimeError(f"train-{phase} checkpoint no longer matches its audit")
    return checkpoint, record


def reserve_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def wait_for_local_server(process: subprocess.Popen, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("private inference server exited during model load")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.25)
    raise RuntimeError("private inference server did not become ready")


def request_wav(port: int, payload: dict[str, object], output: Path) -> float:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/tts",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    started = time.monotonic()
    with opener.open(request, timeout=600) as response:
        if response.status != 200 or response.headers.get_content_type() != "audio/wav":
            raise RuntimeError("private inference request returned a non-WAV response")
        audio = response.read()
    elapsed = time.monotonic() - started
    output.write_bytes(audio)
    return elapsed


def evaluate(args: argparse.Namespace) -> int:
    root, python, _selected, raw_dir, run, worklog_path, worklog, _names = load_context(
        args
    )
    if args.eval_list is None:
        raise ValueError("--eval-list is required for evaluate")
    eval_list = args.eval_list.resolve()
    selection = json.loads(args.selection_audit.resolve().read_text(encoding="utf-8"))
    if not eval_list.is_file():
        raise ValueError("evaluation list is missing")
    if selection.get("private_artifact_sha256", {}).get("eval") != sha256_file(
        eval_list
    ):
        raise ValueError("evaluation list hash does not match its selection audit")

    s2_checkpoint, s2_record = checkpoint_from_audit(run, "s2")
    s1_checkpoint, s1_record = checkpoint_from_audit(run, "s1")
    reference, prompt_language, prompt_text, reference_duration = select_reference(
        eval_list, raw_dir
    )
    if prompt_language != "zh":
        raise ValueError("selected smoke reference is not Chinese")

    private_config = yaml.safe_load(
        (root / "GPT_SoVITS/configs/tts_infer.yaml").read_text(encoding="utf-8")
    )
    private_config["custom"].update(
        {
            "device": "cuda:0",
            "is_half": True,
            "version": "v2Pro",
            "t2s_weights_path": str(s1_checkpoint),
            "vits_weights_path": str(s2_checkpoint),
            "bert_base_path": str(
                root / "GPT_SoVITS/pretrained_models/chinese-roberta-wwm-ext-large"
            ),
            "cnhuhbert_base_path": str(
                root / "GPT_SoVITS/pretrained_models/chinese-hubert-base"
            ),
        }
    )
    config_path = run / "tts-infer-private.yaml"
    config_path.write_text(
        yaml.safe_dump(private_config, sort_keys=False), encoding="utf-8"
    )

    payload = {
        "text": args.target_text,
        "text_lang": "zh",
        "ref_audio_path": str(reference),
        "prompt_text": prompt_text,
        "prompt_lang": prompt_language,
        "text_split_method": "cut5",
        "batch_size": 1,
        "split_bucket": False,
        "speed_factor": 1.0,
        "seed": 1234,
        "streaming_mode": 0,
        "media_type": "wav",
        "parallel_infer": False,
    }
    request_path = run / "smoke-request-private.json"
    atomic_json(request_path, payload)

    log_path = run / "logs" / "inference-smoke.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    python_path = [str(root), str(root / "GPT_SoVITS")]
    if env.get("PYTHONPATH"):
        python_path.append(env["PYTHONPATH"])
    env.update(
        {
            "CUDA_VISIBLE_DEVICES": "0",
            "_CUDA_VISIBLE_DEVICES": "0",
            "MPLCONFIGDIR": str((args.cache_dir / "matplotlib").resolve()),
            "NUMBA_CACHE_DIR": str((args.cache_dir / "numba").resolve()),
            "HF_HOME": str((args.cache_dir / "huggingface").resolve()),
            "TOKENIZERS_PARALLELISM": "false",
            "PYTHONPATH": os.pathsep.join(python_path),
        }
    )
    port = reserve_local_port()
    first_wav = run / "smoke-first.wav"
    warm_wav = run / "smoke-warm.wav"
    load_started = time.monotonic()
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [
                str(python),
                "api_v2.py",
                "-a",
                "127.0.0.1",
                "-p",
                str(port),
                "-c",
                str(config_path),
            ],
            cwd=root,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            wait_for_local_server(process, port, args.api_start_timeout)
            load_seconds = time.monotonic() - load_started
            first_seconds = request_wav(port, payload, first_wav)
            warm_seconds = request_wav(port, payload, warm_wav)
        finally:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=15)

    first_properties = wave_properties(first_wav)
    warm_properties = wave_properties(warm_wav)
    if (
        first_properties["channels"] != 1
        or first_properties["sample_width"] != 2
        or warm_properties["channels"] != 1
        or warm_properties["sample_width"] != 2
    ):
        raise RuntimeError("V2Pro smoke output is not mono 16-bit PCM WAV")
    if shutil.which("ffmpeg") is None:
        raise RuntimeError("ffmpeg is required for the AIDK playback artifact")
    aidk_wav = run / "smoke-aidk-16k-mono-s16.wav"
    ffmpeg_log_path = run / "logs" / "inference-ffmpeg.log"
    with ffmpeg_log_path.open("w", encoding="utf-8") as ffmpeg_log:
        conversion = subprocess.run(
            [
                "ffmpeg",
                "-nostdin",
                "-y",
                "-i",
                str(warm_wav),
                "-map",
                "0:a:0",
                "-ac",
                "1",
                "-ar",
                "16000",
                "-c:a",
                "pcm_s16le",
                str(aidk_wav),
            ],
            stdout=ffmpeg_log,
            stderr=subprocess.STDOUT,
            check=False,
            text=True,
        )
    aidk_properties = wave_properties(aidk_wav) if conversion.returncode == 0 else {}
    if (
        conversion.returncode != 0
        or aidk_properties.get("channels") != 1
        or aidk_properties.get("sample_width") != 2
        or aidk_properties.get("sample_rate") != 16000
    ):
        raise RuntimeError("AIDK 16 kHz mono S16 conversion failed")

    duration = float(warm_properties["duration_seconds"])
    audit = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created_utc(),
        "phase": "evaluate",
        "profile": "v2Pro-fp16-single-gpu-local-api",
        "status": "PASS",
        "reference": {
            "audio_sha256": sha256_file(reference),
            "duration_seconds": round(reference_duration, 6),
            "prompt_text_sha256": hashlib.sha256(
                prompt_text.encode("utf-8")
            ).hexdigest(),
        },
        "target_text_sha256": hashlib.sha256(
            args.target_text.encode("utf-8")
        ).hexdigest(),
        "checkpoints": {"s2": s2_record["sha256"], "s1": s1_record["sha256"]},
        "timing": {
            "model_load_seconds": round(load_seconds, 6),
            "first_request_seconds": round(first_seconds, 6),
            "warm_request_seconds": round(warm_seconds, 6),
            "audio_duration_seconds": round(duration, 6),
            "first_request_rtf": round(first_seconds / duration, 6),
            "warm_request_rtf": round(warm_seconds / duration, 6),
        },
        "outputs": {
            "native": {"sha256": sha256_file(warm_wav), **warm_properties},
            "aidk": {"sha256": sha256_file(aidk_wav), **aidk_properties},
        },
        "private_log_sha256": sha256_file(log_path),
        "ffmpeg_log_sha256": sha256_file(ffmpeg_log_path),
    }
    audit_path = run / "inference-smoke-audit.json"
    atomic_json(audit_path, audit)
    worklog["status"] = "VOICE_MODEL_SMOKE_COMPLETE"
    worklog["stages"]["inference_smoke"] = "PASS"
    worklog["events"].append(
        {
            "created_utc": audit["created_utc"],
            "event": "gpt_sovits_inference_smoke",
            "status": "PASS",
            "audit_sha256": sha256_file(audit_path),
            "warm_rtf": audit["timing"]["warm_request_rtf"],
        }
    )
    for candidate in worklog.get("model_candidates", []):
        if candidate.get("id") == "gpt-sovits-finetune":
            candidate["status"] = "INFERENCE_SMOKE_READY"
    atomic_json(worklog_path, worklog)
    print(
        "BKVOICE_GSV_EVALUATE_PASS "
        + json.dumps(
            {
                "native_rate": warm_properties["sample_rate"],
                "aidk_rate": aidk_properties["sample_rate"],
                "duration_seconds": audit["timing"]["audio_duration_seconds"],
                "warm_rtf": audit["timing"]["warm_request_rtf"],
                "aidk_sha256": audit["outputs"]["aidk"]["sha256"],
            },
            sort_keys=True,
        )
    )
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "phase", choices=("preprocess", "configure", "train-s2", "train-s1", "evaluate")
    )
    parser.add_argument("--python", required=True, type=Path)
    parser.add_argument("--gpt-sovits-root", required=True, type=Path)
    parser.add_argument("--selected-list", required=True, type=Path)
    parser.add_argument("--selection-audit", required=True, type=Path)
    parser.add_argument("--raw-dir", required=True, type=Path)
    parser.add_argument("--worklog", required=True, type=Path)
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--eval-list", type=Path)
    parser.add_argument("--target-text", default="你好，今天也要一起去看更远的风景。")
    parser.add_argument("--api-start-timeout", type=float, default=300.0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--experiment-name", default="bkvoice-v2pro-r1")
    parser.add_argument("--batch-size", type=int, default=1)
    parser.add_argument("--s2-epochs", type=int, default=1)
    parser.add_argument("--s1-epochs", type=int, default=1)
    args = parser.parse_args(argv)
    if args.batch_size <= 0 or args.s2_epochs <= 0 or args.s1_epochs <= 0:
        parser.error("batch size and epochs must be positive")
    if args.api_start_timeout <= 0:
        parser.error("API start timeout must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    try:
        if args.phase == "preprocess":
            return preprocess(args)
        if args.phase == "configure":
            return configure(args)
        if args.phase == "evaluate":
            return evaluate(args)
        return run_training(args, args.phase.removeprefix("train-"))
    except (OSError, ValueError, RuntimeError) as error:
        print(
            f"BKVOICE_GSV_PIPELINE_FAIL phase={args.phase} error={type(error).__name__}"
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
