#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Synthetic privacy and association tests for the WeChat voice manifest."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
TOOL = REPOSITORY / "tools/bkvoice/wechat_voice_manifest.py"
WORKLOG_TOOL = REPOSITORY / "tools/bkvoice/bkvoice_training_worklog.py"
CORPUS_TOOL = REPOSITORY / "tools/bkvoice/bkvoice_prepare_corpus.py"
GSV_PIPELINE_TOOL = REPOSITORY / "tools/bkvoice/bkvoice_gpt_sovits_pipeline.py"


def write_wav(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(24000)
        audio.writeframes(b"\0\0" * 240)


def fixture(root: Path, group: bool = False) -> None:
    (root / "manifest.json").write_text(
        json.dumps({"isGroup": group}), encoding="utf-8"
    )
    page = root / "pages/page-0001.js"
    page.parent.mkdir(parents=True)
    page.write_text(
        "wce-msg-row-sent wechat-voice-sent media/sent.wav\n"
        "wce-msg-row-received wechat-voice-received media/received.wav\n"
        "wce-msg-row-sent wechat-voice-sent media/collision.wav\n"
        "wce-msg-row-received wechat-voice-received media/collision.wav\n",
        encoding="utf-8",
    )
    for name in ("sent.wav", "received.wav", "collision.wav", "orphan.wav"):
        write_wav(root / "media" / name)


def run(root: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), "--export-root", str(root), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def run_worklog(*extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(WORKLOG_TOOL), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def run_corpus(*extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CORPUS_TOOL), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def load_gsv_pipeline():
    spec = importlib.util.spec_from_file_location(
        "bkvoice_gsv_pipeline", GSV_PIPELINE_TOOL
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_default_received_excludes_collision() -> None:
    with tempfile.TemporaryDirectory(prefix="wechat-voice-manifest-") as temporary:
        root = Path(temporary) / "export"
        root.mkdir()
        fixture(root)
        out = Path(temporary) / "manifest.jsonl"
        audit = Path(temporary) / "audit.json"
        result = run(root, "--out", str(out), "--audit-out", str(audit))
        assert result.returncode == 0, result.stderr
        rows = [
            json.loads(line) for line in out.read_text(encoding="utf-8").splitlines()
        ]
        assert len(rows) == 1
        assert rows[0]["direction"] == "received"
        assert rows[0]["association_status"] == "associated"
        assert rows[0]["sample_rate"] == 24000
        assert rows[0]["channels"] == 1
        assert "path_sha256" in rows[0] and "path" not in rows[0]
        assert "received.wav" not in out.read_text(encoding="utf-8")
        report = json.loads(audit.read_text(encoding="utf-8"))
        assert report["direct_chat"] is True
        assert report["failure_reasons"]["direction_collision"] == 1
        assert report["private_output_sha256"] is None


def test_private_map_is_explicit_and_kept_separate() -> None:
    with tempfile.TemporaryDirectory(prefix="wechat-voice-manifest-") as temporary:
        root = Path(temporary) / "export"
        root.mkdir()
        fixture(root)
        out = Path(temporary) / "manifest.jsonl"
        private = Path(temporary) / "manifest.private.jsonl"
        audit = Path(temporary) / "audit.json"
        result = run(
            root,
            "--out",
            str(out),
            "--private-out",
            str(private),
            "--audit-out",
            str(audit),
        )
        assert result.returncode == 0, result.stderr
        public_row = json.loads(out.read_text(encoding="utf-8").splitlines()[0])
        private_row = json.loads(private.read_text(encoding="utf-8").splitlines()[0])
        assert "source_path" not in public_row
        assert private_row["source_path"] == "media/received.wav"
        assert private_row["sha256"] == public_row["sha256"]
        report = json.loads(audit.read_text(encoding="utf-8"))
        assert report["private_output_sha256"] is not None

        worklog = Path(temporary) / "worklog.private.json"
        result = run_worklog(
            "--source-audit",
            str(audit),
            "--private-manifest",
            str(private),
            "--speaker-id",
            "private_voice_01",
            "--consent-status",
            "user-attested",
            "--out",
            str(worklog),
        )
        assert result.returncode == 0, result.stderr
        record = json.loads(worklog.read_text(encoding="utf-8"))
        assert record["status"] == "SOURCE_AUDITED"
        assert record["source"]["utterance_count"] == 1
        assert record["source"]["duration_ms"] == 10.0
        assert record["stages"]["finetune"] == "NOT_STARTED"
        serialized = worklog.read_text(encoding="utf-8")
        assert "received.wav" not in serialized and "source_path" not in serialized

        corpus = Path(temporary) / "corpus.private"
        result = run_corpus(
            "--export-root",
            str(root),
            "--private-manifest",
            str(private),
            "--worklog",
            str(worklog),
            "--out-dir",
            str(corpus),
            "--min-duration-ms",
            "1",
            "--max-duration-ms",
            "100",
            "--min-rms-dbfs",
            "-100",
            "--min-active-ratio",
            "0",
        )
        assert result.returncode == 0, result.stderr
        corpus_audit = json.loads(
            (corpus / "corpus-audit.json").read_text(encoding="utf-8")
        )
        assert corpus_audit["counts"]["accepted"] == 1
        assert corpus_audit["counts"]["rejected"] == 0
        assert len(list((corpus / "raw").iterdir())) == 1
        record = json.loads(worklog.read_text(encoding="utf-8"))
        assert record["status"] == "CORPUS_PREPARED"
        assert record["stages"]["quality_filter"] == "PASS"
        assert record["stages"]["dataset_split"] == "PASS"
        assert "received.wav" not in worklog.read_text(encoding="utf-8")

        private.write_text(private.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        tampered = Path(temporary) / "tampered-worklog.json"
        result = run_worklog(
            "--source-audit",
            str(audit),
            "--private-manifest",
            str(private),
            "--speaker-id",
            "private_voice_01",
            "--consent-status",
            "user-attested",
            "--out",
            str(tampered),
        )
        assert result.returncode != 0
        assert not tampered.exists()


def test_unknown_includes_orphan_but_default_excludes_collision() -> None:
    with tempfile.TemporaryDirectory(prefix="wechat-voice-manifest-") as temporary:
        root = Path(temporary) / "export"
        root.mkdir()
        fixture(root)
        out = Path(temporary) / "manifest.jsonl"
        audit = Path(temporary) / "audit.json"
        result = run(
            root, "--direction", "unknown", "--out", str(out), "--audit-out", str(audit)
        )
        assert result.returncode == 0, result.stderr
        rows = [
            json.loads(line) for line in out.read_text(encoding="utf-8").splitlines()
        ]
        assert [row["association_status"] for row in rows] == ["unreferenced_wav"]


def test_group_export_fails_closed_and_dry_run_writes_nothing() -> None:
    with tempfile.TemporaryDirectory(prefix="wechat-voice-manifest-") as temporary:
        root = Path(temporary) / "export"
        root.mkdir()
        fixture(root, group=True)
        result = run(root, "--dry-run")
        assert result.returncode == 2
        report = json.loads(result.stdout)
        assert report["direct_chat"] is False
        assert report["failure_reasons"]["not_direct_chat"] == 1
        assert not list(root.glob("*.jsonl"))


def test_refuses_to_write_into_export() -> None:
    with tempfile.TemporaryDirectory(prefix="wechat-voice-manifest-") as temporary:
        root = Path(temporary) / "export"
        root.mkdir()
        fixture(root)
        result = run(
            root,
            "--out",
            str(root / "manifest.jsonl"),
            "--audit-out",
            str(Path(temporary) / "audit.json"),
        )
        assert result.returncode != 0
        assert not (root / "manifest.jsonl").exists()

        result = run(
            root,
            "--out",
            str(Path(temporary) / "manifest.jsonl"),
            "--audit-out",
            str(Path(temporary) / "audit.json"),
            "--private-out",
            str(root / "private.jsonl"),
        )
        assert result.returncode != 0
        assert not (root / "private.jsonl").exists()


def test_gpt_sovits_reference_and_checkpoint_gates() -> None:
    pipeline = load_gsv_pipeline()
    with tempfile.TemporaryDirectory(prefix="bkvoice-gsv-gates-") as temporary:
        root = Path(temporary)
        raw = root / "raw"
        short = raw / "short.wav"
        reference = raw / "reference.wav"
        write_wav(short)
        reference.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(reference), "wb") as audio:
            audio.setnchannels(1)
            audio.setsampwidth(2)
            audio.setframerate(16000)
            audio.writeframes(b"\0\0" * 96000)
        selected = root / "selected-eval.list"
        selected.write_text(
            f"{short}|speaker|ZH|synthetic short fixture\n"
            f"{reference}|speaker|ZH|synthetic valid fixture\n",
            encoding="utf-8",
        )
        path, language, prompt, duration = pipeline.select_reference(selected, raw)
        assert path == reference
        assert language == "zh"
        assert prompt == "synthetic valid fixture"
        assert duration == 6.0

        run = root / "run"
        weights = run / "weights/s2"
        weights.mkdir(parents=True)
        checkpoint = weights / "model.pth"
        checkpoint.write_bytes(b"checkpoint")
        record = {
            "name": checkpoint.name,
            "size": checkpoint.stat().st_size,
            "sha256": pipeline.sha256_file(checkpoint),
        }
        (run / "train-s2-audit.json").write_text(
            json.dumps({"status": "PASS", "checkpoints": [record]}), encoding="utf-8"
        )
        selected_checkpoint, selected_record = pipeline.checkpoint_from_audit(run, "s2")
        assert selected_checkpoint == checkpoint
        assert selected_record == record
        checkpoint.write_bytes(b"tampered")
        try:
            pipeline.checkpoint_from_audit(run, "s2")
        except RuntimeError:
            pass
        else:
            raise AssertionError("tampered checkpoint was accepted")


def main() -> int:
    test_default_received_excludes_collision()
    test_private_map_is_explicit_and_kept_separate()
    test_unknown_includes_orphan_but_default_excludes_collision()
    test_group_export_fails_closed_and_dry_run_writes_nothing()
    test_refuses_to_write_into_export()
    test_gpt_sovits_reference_and_checkpoint_gates()
    print("WECHAT_VOICE_MANIFEST_TEST_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
