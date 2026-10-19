# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import importlib
from pathlib import Path
import struct
import sys
import subprocess
import tempfile
import unittest
import wave
from contextlib import contextmanager
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))

hil_test = importlib.import_module("_lib.hil_test")
workbench = importlib.import_module("_lib.workbench")


class FakeClient:
    def __init__(self):
        self.records = []
        self.snapshot = {
            "enabled": True,
            "session": 0,
            "sequence": 0,
            "elapsed_ms": 0,
            "key_mask": 0,
            "pm_mode": "blocked",
            "pm_requests": 0,
            "pm_queries": 0,
            "power_state": "idle",
            "power_unresolved": False,
            "power_error": 0,
            "last_result": 0,
        }

    def engineering_status(self):
        return dict(self.snapshot)

    def engineering_command(self, record):
        decoded = hil_test.decode_command(record)
        self.records.append(decoded)
        self.snapshot.update(
            session=decoded["session"],
            sequence=decoded["sequence"],
            elapsed_ms=decoded["elapsed_ms"],
        )
        if decoded["operation"] == "session":
            self.snapshot["pm_mode"] = decoded["pm_mode"]
        elif decoded["operation"] == "key":
            self.snapshot["key_mask"] = decoded["key_mask"]
        return {"accepted": True, "completion_verified": False}


class HilTestContract(unittest.TestCase):
    def test_audio_stream_separates_network_queue_and_consumer(self):
        # Independent clocks describe a remote wait, then a blocked queue.
        events = [
            [1, 1, 100, 100, 1, 0],
            [2, 2, 110, 210, 1024, 0],
            [3, 3, 211, 212, 768, 0],
            [4, 4, 212, 213, 512, 512],
            [5, 5, 213, 229, 512, 512],
            [6, 2, 230, 3230, 1024, 0],
            [7, 3, 3231, 3332, 768, 0],
            [8, 4, 3232, 3332, 512, 65536],
            [9, 6, 3333, 3333, 1, 0],
            [10, 7, 3340, 3340, 1, 0],
            [11, 8, 3350, 3350, 3, 65536],
        ]
        trace = dict(
            request_id=7,
            events=events,
            dropped=0,
            rearm_state=2,
            rearm_attempts=1,
            rearm_ms=4000,
            sealed=True,
        )
        report = hil_test.audio_stream_report(trace)
        self.assertTrue(report["observation_complete"])
        self.assertEqual(report["network_read_max_ms"], 3000)
        self.assertEqual(report["max_pcm_gap_ms"], 3020)
        self.assertEqual(report["queue_wait_max_ms"], 100)
        self.assertEqual(report["media_write_max_ms"], 16)
        self.assertEqual(report["queue_peak"], 65536)
        self.assertEqual(report["buffer_underflow"], 1)
        self.assertIsNone(report["dac_underruns"])
        self.assertEqual(report["kws_rearm"], True)
        gap = hil_test.audio_gap_report([report])["turns"][0]
        self.assertEqual(gap["max_gap"]["duration_ms"], 3020)
        self.assertEqual(gap["max_gap"]["tls_read_overlap_ms"], 3000)
        # The same read-entry/return span can include a descheduled device
        # thread. A long call cannot eliminate local processing/scheduling.
        self.assertEqual(gap["classification"], "receive_call_wait")
        self.assertEqual(gap["candidate_sources"], ["A", "B", "C"])
        self.assertIsNone(gap["root_cause"])
        self.assertEqual(gap["pcm_chunks"][0]["media_consume_time"], 213)
        self.assertEqual(gap["pcm_chunks"][0]["tls_receive_time"], 210)
        self.assertEqual(gap["queue_starvation_count"], 0)
        self.assertEqual(gap["queue_tail_wait_count"], 1)
        self.assertEqual(gap["queue_empty_waits"][0]["duration_ms"], 17)
        resumed = dict(
            trace,
            events=[
                [1, 1, 0, 0, 1, 0],
                [2, 4, 10, 10, 512, 512],
                [3, 5, 11, 27, 512, 512],
                [4, 6, 28, 28, 1, 0],
                [5, 4, 300, 300, 512, 512],
                [6, 5, 301, 317, 512, 512],
                [7, 8, 318, 318, 2, 512],
            ],
        )
        resumed_gap = hil_test.audio_gap_report(
            [hil_test.audio_stream_report(resumed)]
        )["turns"][0]
        self.assertEqual(resumed_gap["queue_starvation_count"], 1)
        self.assertEqual(resumed_gap["queue_tail_wait_count"], 0)
        self.assertEqual(resumed_gap["queue_empty_waits"][0]["duration_ms"], 273)
        trace["dropped"] = 1
        self.assertFalse(hil_test.audio_stream_report(trace)["observation_complete"])
        incomplete = hil_test.audio_gap_report([hil_test.audio_stream_report(trace)])
        self.assertEqual(incomplete["turns"][0]["classification"], "incomplete")
        self.assertEqual(incomplete["turns"][0]["candidate_sources"], [])
        trace["dropped"] = 0
        local_trace = dict(
            trace,
            events=[
                [1, 1, 0, 0, 1, 0],
                [2, 4, 100, 100, 512, 512],
                [3, 2, 101, 120, 5120, 0],
                [4, 5, 150, 170, 512, 512],
                [5, 4, 3010, 3011, 512, 512],
                [6, 3, 3000, 3011, 768, 0],
                [7, 7, 3020, 3020, 1, 0],
                [8, 8, 3100, 3100, 2, 512],
            ],
        )
        local = hil_test.audio_gap_report([hil_test.audio_stream_report(local_trace)])
        self.assertEqual(
            local["turns"][0]["classification"], "device_pre_pcm_no_receive"
        )
        self.assertEqual(local["turns"][0]["candidate_sources"], ["C"])
        self.assertEqual(
            local["turns"][0]["max_gap"]["post_receive_to_decode_ms"], 2880
        )
        self.assertIsNone(local["turns"][0]["root_cause"])
        trace["events"][3][0] = 8
        with self.assertRaises(hil_test.HilTestError):
            hil_test.audio_stream_report(trace)

    def test_audio_stream_keeps_tts_isolation_diagnostic_spans_separate(self):
        trace = dict(
            request_id=9,
            dropped=0,
            rearm_state=2,
            rearm_attempts=1,
            rearm_ms=1,
            sealed=True,
            events=[
                [1, 1, 0, 0, 1, 0],
                [2, 9, 10, 11, 512, 0],
                [3, 10, 11, 30, 0, 0],
                [4, 11, 25, 31, 0, 0],
                [5, 12, 31, 32, 0, 0],
                [6, 13, 32, 34, 512, 0],
                [7, 4, 35, 35, 512, 512],
                [8, 5, 36, 40, 512, 0],
                [9, 8, 41, 41, 2, 0],
            ],
        )
        report = hil_test.audio_stream_report(trace)
        self.assertTrue(report["observation_complete"])
        self.assertEqual(
            [(span["kind"], span["name"]) for span in report["diagnostic_spans"]],
            [
                (9, "socket_recv"),
                (10, "socket_readiness_wait"),
                (11, "readiness_to_poll_teardown"),
                (12, "crypto_lock"),
                (13, "ssl_read"),
            ],
        )
        self.assertEqual(report["max_pcm_gap_ms"], 0)

    def test_audio_stream_missing_is_not_zero_and_sentences_are_distinct(self):
        trace = dict(
            request_id=1,
            events=[],
            dropped=0,
            rearm_state=0,
            rearm_attempts=0,
            rearm_ms=0,
            sealed=False,
        )
        report = hil_test.audio_stream_report(trace)
        self.assertIsNone(report["buffer_underflow"])
        self.assertIsNone(report["max_pcm_gap_ms"])
        self.assertFalse(report["observation_complete"])
        trace.update(
            sealed=True,
            events=[
                [1, 1, 0, 0, 1, 0],
                [2, 4, 100, 100, 512, 512],
                [3, 7, 101, 101, 1, 0],
                [4, 1, 200, 200, 2, 0],
                [5, 4, 3200, 3200, 512, 512],
                [6, 7, 3300, 3300, 2, 0],
            ],
        )
        report = hil_test.audio_stream_report(trace)
        self.assertEqual(report["max_pcm_gap_ms"], 3100)
        self.assertEqual(report["max_pcm_gap_within_tts_ms"], 0)
        self.assertEqual(report["max_pcm_gap_between_tts_ms"], 3100)
        gap = hil_test.audio_gap_report([report])["turns"][0]
        self.assertFalse(gap["max_gap"]["same_tts_request"])
        self.assertEqual(gap["max_gap"]["between_requests_ms"], 99)
        self.assertEqual(gap["classification"], "incomplete")
        self.assertIsNone(gap["pcm_chunks"][0]["media_consume_time"])
        report.update(input_sha256="a" * 64, observation_complete=True)
        previous = dict(report, max_pcm_gap_ms=4000)
        comparison = hil_test.compare_audio_stream([report], dict(turns=[previous]))
        self.assertEqual(comparison[0]["delta"]["max_pcm_gap_ms"], -900)
        previous["input_sha256"] = "b" * 64
        comparison = hil_test.compare_audio_stream([report], dict(turns=[previous]))
        self.assertFalse(comparison[0]["comparable"])
        self.assertIsNone(comparison[0]["delta"]["max_pcm_gap_ms"])

    def test_audio_trace_usb_checks_identity_bounds_and_sealed_records(self):
        header = struct.pack(
            ">4s15I", b"BVA2", 2, 0, 7, 1, 0, 2, 1, 400, 1, 0, 0, 0, 0, 0, 0
        )
        record = struct.pack(">6I", 1, 4, 100, 150, 512, 65536)
        wire = header + record
        client = SimpleNamespace(
            closed=False, authenticated=True, _timeout=1, _now=lambda: 0
        )
        offsets = []

        def exchange(command, payload, deadline):
            (value,) = struct.unpack(">I", payload)
            self.assertEqual(command, 15)
            self.assertEqual(value >> 16, 24)
            offset = ((value & 0xFFFF) - 256) * 16
            offsets.append(offset)
            return (
                len(wire),
                *struct.unpack(">4I", wire[offset : offset + 16].ljust(16, b"\0")),
            )

        client._exchange = exchange
        client._audio_trace_pages = lambda pages: [
            exchange(15, struct.pack(">I", 24 << 16 | page), 1) for page in pages
        ]
        trace = workbench.ControlClient.engineering_audio_trace(client, 7)
        self.assertEqual(trace["events"], [[1, 4, 100, 150, 512, 65536]])
        self.assertEqual(offsets, [0, 16, 32, 48, 64, 80])
        with self.assertRaises(workbench.ControlError):
            workbench.ControlClient.engineering_audio_trace(client, 8)
        progress = {}

        def interrupted(pages):
            raise workbench.ControlError("fixture transport interruption")

        client._audio_trace_pages = interrupted
        with self.assertRaises(workbench.ControlError):
            workbench.ControlClient.engineering_audio_trace(
                client, 7, progress=progress
            )
        self.assertEqual(progress["expected_records"], 1)
        self.assertEqual(progress["header"]["request_id"], 7)
        self.assertFalse(progress["readback_complete"])
        self.assertEqual(progress["events"], [])
        client.authenticated = False
        with self.assertRaises(workbench.ControlError):
            workbench.ControlClient.engineering_audio_trace(client, 7)

    def test_voice_benchmark_inputs_preserve_pcm_and_bound_dialogue(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            wav = folder / "sample.wav"
            pcm = b"\x00\x01" * 320
            with wave.open(str(wav), "wb") as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(16000)
                audio.writeframes(pcm)
            turns = hil_test.voice_inputs(wav=wav, repeat=10)
            self.assertEqual(len(turns), 10)
            self.assertTrue(all(turn["payload"] == pcm for turn in turns))
            self.assertEqual(turns[0]["mode"], 2)
            dialogue = folder / "dialogue.json"
            dialogue.write_text(
                json.dumps([{"text": "fixed input"}, {"wav": "sample.wav"}])
            )
            mixed = hil_test.voice_inputs(dialogue=dialogue)
            self.assertEqual([turn["mode"] for turn in mixed], [1, 2])
            self.assertEqual(mixed[1]["payload"], pcm)
            with self.assertRaises(hil_test.HilTestError):
                hil_test.voice_inputs(wav=wav, text="ambiguous")
            with self.assertRaises(hil_test.HilTestError):
                hil_test.voice_inputs(text="x", repeat=11)
            dialogue.write_text(
                json.dumps([{"wav": "sample.wav", "text": "ambiguous"}])
            )
            with self.assertRaises(hil_test.HilTestError):
                hil_test.voice_inputs(dialogue=dialogue)

    def test_tts_isolation_uses_frozen_text_and_mode_three_only(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / "fixed-tts.txt"
            source.write_text("fixed TTS isolation input", encoding="utf-8")
            calls = []

            class Client:
                def __init__(self):
                    self.status_calls = 0

                def info(self):
                    return {"build": 745}

                def engineering_voice_status(self):
                    self.status_calls += 1
                    return {
                        "session": 0 if self.status_calls == 1 else 1,
                        "mode": 3,
                        "state": 5,
                        "borrowed": False,
                        "request_id": 17,
                        "result": 0,
                    }

                def engineering_voice_command(self, packet):
                    calls.append(hil_test.decode_voice_packet(packet))
                    return {"accepted": True}

                def engineering_audio_trace(self, request_id, *, header_only=False, progress=None):
                    self.assertEqual(request_id, 17)
                    if header_only:
                        return {"rearm_state": 2}
                    return {
                        "request_id": 17,
                        "events": [],
                        "dropped": 0,
                        "rearm_state": 2,
                        "rearm_attempts": 1,
                        "rearm_ms": 1,
                        "sealed": True,
                    }

                def assertEqual(self, left, right):
                    self._case.assertEqual(left, right)

            client = Client()
            client._case = self

            @contextmanager
            def authorized(_):
                yield client

            args = SimpleNamespace(
                git_sha="1" * 40,
                input_text=source,
                cloud_config=None,
                output_dir=folder / "report",
                turn_timeout=180,
                audio_trace=True,
                audio_status=False,
            )
            with patch.object(workbench, "authorized_client", authorized):
                result = hil_test.run_tts_isolation(args)
            report = json.loads((args.output_dir / "voice_tts_isolation.json").read_text())
            self.assertEqual(result["total_ms"], None)
            self.assertEqual(report["mode"], "tts")
            self.assertEqual(report["input"]["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(report["total_ms"], None)
            self.assertEqual({call["mode"] for call in calls}, {3})
            self.assertEqual([call["operation"] for call in calls], [1, 2, 3])

    def test_tts_isolation_text_and_packet_contracts(self):
        self.assertEqual(hil_test.decode_voice_packet(hil_test.voice_packet(1, 1, 1, 3))["mode"], 3)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.txt"
            path.write_bytes(b"\0")
            with self.assertRaises(hil_test.HilTestError):
                hil_test.tts_isolation_input(path)
            path.write_bytes(b"x" * 4097)
            with self.assertRaises(hil_test.HilTestError):
                hil_test.tts_isolation_input(path)

    def test_voice_benchmark_transport_never_replays_unconfirmed_run(self):
        calls = []

        class Client:
            def engineering_voice_command(self, packet):
                words = struct.unpack(">4s7I", packet[:32])
                calls.append(words[2])
                if words[2] == 3:
                    raise workbench.CommandUnconfirmed()
                return {"accepted": True}

        progress = {}
        ticks = iter(i / 100 for i in range(20))
        with patch.object(
            hil_test.time, "perf_counter", side_effect=lambda: next(ticks)
        ):
            with self.assertRaises(workbench.CommandUnconfirmed):
                hil_test.voice_execute(
                    Client(),
                    session=1,
                    mode=1,
                    payload=b"fixed input",
                    progress=progress,
                )
        self.assertEqual(calls, [1, 2, 3])
        self.assertEqual(progress["upload_ms"], 50)
        self.assertEqual(progress["control_command_max_ms"], 10)
        self.assertEqual(progress["last_confirmed_sequence"], 2)
        self.assertEqual(progress["uploaded_bytes"], len(b"fixed input"))
        self.assertIsNone(progress["execution_performed"])

        class StatusTimeoutClient:
            def engineering_voice_command(self, packet):
                return {"accepted": True}

            def engineering_voice_status(self):
                self.last_engineering_voice_status = {
                    "offset": 32,
                    "sdc_sequence": 19,
                }
                raise TimeoutError("fixture timeout")

        progress = {}
        clocks = iter((100.0, 100.125, 100.250))
        with patch.object(hil_test.time, "monotonic", side_effect=lambda: next(clocks)):
            with self.assertRaises(TimeoutError):
                hil_test.voice_execute(
                    StatusTimeoutClient(),
                    session=1,
                    mode=1,
                    payload=b"fixed input",
                    progress=progress,
                )
        self.assertEqual(progress["status_poll_count"], 1)
        self.assertEqual(progress["status_poll_max_ms"], 125)
        self.assertEqual(
            progress["status_polls"],
            [
                {
                    "start_ms": 125,
                    "duration_ms": 125,
                    "offset": 32,
                    "sdc_sequence": 19,
                    "outcome": "TimeoutError",
                }
            ],
        )

        # A full PCM packet must fit the bounded USB transaction. The old
        # 32-byte APPEND loop exhausts 15 seconds on a 150 ms peer.
        tick = [0.0]
        frames = []
        client = SimpleNamespace(
            closed=False,
            authenticated=True,
            _timeout=15,
            _now=lambda: tick[0],
            close=lambda: None,
        )

        def exchange(command, payload, deadline):
            tick[0] += 0.15
            if tick[0] >= deadline:
                raise TimeoutError()
            frames.append((command, payload))
            if command == 15:
                self.assertEqual(payload, struct.pack(">I", 0x7FFF0000))
                return (12, 0x43415031, 1, 512, 0)
            return (0,) * 5

        client._exchange = exchange
        packet = hil_test.voice_packet(2, 1, 2, 2, 0, bytes(range(256)) * 16)
        result = workbench.ControlClient.engineering_voice_command(client, packet)
        self.assertTrue(result["accepted"])
        self.assertEqual(b"".join(p for c, p in frames if c == 17), packet)
        self.assertEqual(sum(c == 17 for c, _ in frames), 9)
        self.assertEqual([c for c, _ in frames].count(18), 1)

        frames.clear()
        client._sequence = 7

        def fail_second_append(command, payload, deadline):
            if command == 17 and sum(c == 17 for c, _ in frames) == 1:
                raise TimeoutError()
            return exchange(command, payload, deadline)

        client._exchange = fail_second_append
        with self.assertRaisesRegex(workbench.CommandUnconfirmed, "append:512"):
            workbench.ControlClient.engineering_voice_command(client, packet)
        self.assertEqual([c for c, _ in frames], [15, 16, 17])

        # A board-observed running turn remains execution evidence when its
        # deadline expires. Preserve the last state and cancel receipt even
        # though no terminal result can be claimed.
        class StalledClient:
            def __init__(self):
                self.running = False

            def info(self):
                return {"build": 717}

            def engineering_voice_command(self, packet):
                if struct.unpack(">4s7I", packet[:32])[2] == 3:
                    self.running = True
                return {"accepted": True}

            def engineering_voice_status(self):
                return dict(
                    session=int(self.running),
                    mode=1,
                    state=4,
                    borrowed=self.running,
                    request_id=0,
                )

        @contextmanager
        def authorized(_):
            yield StalledClient()

        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(
                git_sha="1" * 40,
                wav=None,
                text="fixed",
                dialogue=None,
                repeat=1,
                cloud_config=None,
                output_dir=Path(directory) / "report",
                turn_timeout=180,
                cancel_after_ms=None,
            )
            with patch.object(workbench, "authorized_client", authorized), patch.object(
                hil_test.time, "monotonic", side_effect=[0, 181, 181, 181]
            ):
                with self.assertRaisesRegex(
                    hil_test.HilTestError, "completion unverified"
                ):
                    hil_test.run_benchmark(args)
            result = json.loads((args.output_dir / "benchmark.json").read_text())
            self.assertTrue(result["execution_performed"])
            progress = result["turns"][0]["progress"]
            self.assertTrue(progress["run_confirmed"])
            self.assertTrue(progress["cancel_confirmed"])
            self.assertFalse(progress["completion_verified"])
            self.assertEqual(progress["last_observation"]["state"], 4)

    def test_benchmark_status_only_keeps_usb_rearm_without_claiming_trace(self):
        client = SimpleNamespace(
            info=lambda: {"build": 739},
            engineering_voice_status=lambda: {"session": 0},
        )
        calls = []

        def header(request, *, header_only=False):
            calls.append((request, header_only))
            self.assertTrue(header_only)
            return dict(
                request_id=request,
                rearm_state=2,
                rearm_attempts=2,
                rearm_ms=900,
                dropped=0,
                sealed=True,
            )

        client.engineering_audio_trace = header

        @contextmanager
        def authorized(_):
            yield client

        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(
                git_sha="1" * 40,
                wav=None,
                text="fixed",
                dialogue=None,
                repeat=2,
                cloud_config=None,
                output_dir=Path(directory) / "report",
                turn_timeout=180,
                cancel_after_ms=None,
                audio_status=True,
                audio_trace=False,
            )
            with patch.object(workbench, "authorized_client", authorized), patch.object(
                hil_test,
                "voice_execute",
                side_effect=[
                    {"request_id": 1, "result": 0},
                    {"request_id": 2, "result": 0},
                ],
            ):
                hil_test.run_benchmark(args)
            result = json.loads((args.output_dir / "benchmark.json").read_text())
            self.assertEqual(calls, [(1, True), (2, True)])
            self.assertEqual(
                [t["audio_status"]["rearm_state"] for t in result["turns"]], [2, 2]
            )
            self.assertTrue(all("audio_stream" not in t for t in result["turns"]))
            self.assertEqual(len({t["turn_uid"] for t in result["turns"]}), 2)

    def test_voice_report_injected_input_has_explicit_nonacoustic_origin(self):
        raw = self.voice_log(
            [
                (1000, "input_pcm", 640, 0),
                (1001, "asr_request", 640, 0),
                (1250, "asr_final", 4, 0),
                (2200, "first_media", 2048, 0),
            ]
        )
        turn = hil_test.voice_report(raw, boot_id="injected")["turns"][0]
        self.assertEqual(turn["latency_origin"], "injected-pcm-ready")
        self.assertEqual(turn["asr_final_ms"], 250)
        self.assertEqual(turn["media_first_ms"], 1200)
        self.assertIsNone(turn["total_ms"])

        # A complete no-tool body reuses the planning response; there is no
        # second final request. Its completed planning timestamp is still known.
        raw = self.voice_log(
            [
                (1000, "input_pcm", 640, 0),
                (1100, "plan_request", 1, 0),
                (1400, "plan_response", 1, 0),
                (1405, "final_first", 20, 0),
            ]
        )
        turn = hil_test.voice_report(raw, boot_id="buffered-final")["turns"][0]
        self.assertEqual(turn["planning_complete_ms"], 400)

        # An exclusive final route completes the decision without a native
        # planning call. A tools/invalid route does not complete planning.
        for decision, result, completed in ((1, 0, 200), (0, 0, None), (0, -71, None)):
            raw = self.voice_log(
                [
                    (1000, "input_text", 20, 0),
                    (1100, "route_request", 1, 0),
                    (1200, "route_response", decision, result),
                    (1205, "final_request", 1, 0),
                    (1300, "llm_first_token", 3, 0),
                    (1305, "final_first", 3, 0),
                ]
            )
            turn = hil_test.voice_report(raw, boot_id="routed-final")["turns"][0]
            self.assertEqual(turn["planning_complete_ms"], completed)
            self.assertEqual(turn["llm_first_token_ms"], 300)
            self.assertEqual(
                turn["requests"],
                dict(asr=0, routing=1, voice_stream=0, planning=0, final=1, tts=0),
            )

        raw = self.voice_log(
            [
                (1000, "input_text", 20, 0),
                (1100, "voice_stream_request", 1, 0),
                (1200, "voice_phase_final", 1, 0),
                (1210, "llm_first_token", 3, 0),
                (1215, "final_first", 3, 0),
                (1900, "voice_stream_response", 1, 0),
            ]
        )
        turn = hil_test.voice_report(raw, boot_id="one-stream")["turns"][0]
        self.assertEqual(turn["planning_complete_ms"], 200)
        self.assertEqual(
            turn["requests"],
            dict(asr=0, routing=0, voice_stream=1, planning=0, final=0, tts=0),
        )

    def test_voice_report_cli_writes_separate_artifacts_without_a_port(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            raw = folder / "serial.raw"
            raw.write_text(self.voice_log([(1000, "first_media", 2048, 0)]))
            output = folder / "report"
            command = [
                sys.executable,
                str(ROOT / "tools/bk7258/bk7258.py"),
                "hil-test",
                "voice-report",
                "--from-log",
                str(raw),
                "--boot-id",
                "boot-001",
                "--git-sha",
                "1" * 40,
                "--output-dir",
                str(output),
            ]
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((output / "voice_latency.json").read_text())
            self.assertFalse(report["execution_performed"])
            self.assertIsNone(report["turns"][0]["total_ms"])
            self.assertTrue((output / "turn_latency.csv").is_file())
            self.assertTrue((output / "SHA256SUMS.txt").is_file())
            before = (output / "voice_latency.json").read_bytes()
            repeated = subprocess.run(
                command, cwd=folder, capture_output=True, text=True
            )
            self.assertNotEqual(repeated.returncode, 0)
            self.assertEqual((output / "voice_latency.json").read_bytes(), before)

    @staticmethod
    def voice_log(events, request=7):
        return "\n".join(
            f"[voice-stage] request={request} mono_ms={ms} "
            f"stage={stage} value={value} result={result}"
            for ms, stage, value, result in events
        )

    def test_voice_report_never_substitutes_media_for_acoustic_onset(self):
        log = self.voice_log(
            [
                (1900, "endpoint_detected", 900, 0),
                (2000, "asr_final", 18, 0),
                (3000, "first_media", 2048, 0),
                (5000, "channel_idle", 0, 0),
            ]
        )
        turn = hil_test.voice_report(log, boot_id="boot-a")["turns"][0]
        self.assertIsNone(turn["input_end_mono_ms"])
        self.assertIsNone(turn["asr_final_ms"])
        self.assertIsNone(turn["total_ms"])
        self.assertEqual(turn["events"][0]["mono_ms"], 1900)
        self.assertIsNone(turn["dac_underruns"])
        self.assertIsNone(turn["media_closed"])

    def test_voice_report_correlates_origin_and_counts_real_requests(self):
        events = [
            (900, "asr_request", 32000, 0),
            (1500, "asr_final", 18, 0),
            (1600, "plan_request", 1, 0),
            (1800, "plan_response", 1, -110),
            (1810, "plan_request", 2, 0),
            (2000, "plan_response", 2, 0),
            (2010, "final_request", 1, 0),
            (2100, "final_first", 3, 0),
            (2200, "tts_submit", 18, 0),
            (2400, "first_pcm", 512, 0),
            (2500, "first_media", 2048, 0),
            (2600, "final_response", 1, 0),
            (3000, "tts_submit", 36, 0),
            (5000, "playback_end", 32000, 0),
            (5010, "channel_idle", 0, 0),
            (5050, "rearm", 1, 0),
        ]
        mark = dict(
            request_id=7,
            input_mode="wav",
            input_end_mono_ms=1000,
            clock_id="ap:boot-a",
            uncertainty_ms=2,
            input_evidence="sample-001:annotated-end",
            effective_audio_mono_ms=2520,
            audio_evidence="loopback-001:answer-onset",
            dac_output_mono_ms=2505,
            dac_evidence="driver-start-001:exclusive-benchmark-output",
        )
        report = hil_test.voice_report(
            self.voice_log(events), boot_id="boot-a", input_marks=[mark]
        )
        turn = report["turns"][0]
        self.assertEqual(turn["asr_start_ms"], -100)
        self.assertEqual(turn["asr_final_ms"], 500)
        self.assertEqual(turn["planning_complete_ms"], 1000)
        self.assertEqual(turn["media_first_ms"], 1500)
        self.assertEqual(turn["dac_start_ms"], 1505)
        self.assertEqual(turn["dac_evidence"], mark["dac_evidence"])
        self.assertEqual(turn["total_ms"], 1520)
        self.assertEqual(turn["turn_complete_ms"], 4010)
        self.assertEqual(
            turn["requests"],
            dict(asr=1, routing=0, voice_stream=0, planning=2, final=1, tts=2),
        )
        self.assertTrue(turn["kws_rearm"])
        self.assertIsNone(turn["llm_first_token_ms"])
        self.assertIsNone(turn["agent_ready_ms"])
        self.assertEqual(report["measurement_source"], "captured-log-and-annotations")

    def test_voice_report_rejects_cross_clock_and_unattributed_marks(self):
        log = self.voice_log([(1500, "asr_final", 18, 0)])
        mark = dict(
            request_id=7,
            input_mode="wav",
            input_end_mono_ms=1000,
            clock_id="host:boot-a",
            uncertainty_ms=0,
            input_evidence="sample-001",
        )
        with self.assertRaises(hil_test.HilTestError):
            hil_test.voice_report(log, boot_id="boot-a", input_marks=[mark])
        mark["clock_id"] = "ap:boot-a"
        mark["request_id"] = 8
        with self.assertRaises(hil_test.HilTestError):
            hil_test.voice_report(log, boot_id="boot-a", input_marks=[mark])
        mark["request_id"] = 7
        mark["effective_audio_mono_ms"] = 1600
        with self.assertRaises(hil_test.HilTestError):
            hil_test.voice_report(log, boot_id="boot-a", input_marks=[mark])

    def test_voice_report_preserves_cancellation_and_isolates_turns(self):
        log = self.voice_log(
            [
                (100, "asr_preconnect", 1, 0),
            ],
            request=0,
        )
        log += "\n" + self.voice_log(
            [
                (1000, "asr_request", 640, 0),
                (2000, "asr_final", 0, -125),
                (2005, "queue_peak", 4096, -125),
                (2010, "channel_idle", 0, -125),
                (2011, "rearm", 0, -16),
                (2012, "rearm", 1, 0),
            ]
        )
        log += "\n" + self.voice_log(
            [
                (3000, "asr_request", 640, 0),
                (3500, "asr_final", 18, 0),
            ],
            request=8,
        )
        turns = hil_test.voice_report(log, boot_id="boot-a")["turns"]
        self.assertEqual(len(turns), 2)
        self.assertEqual(turns[0]["result"], "canceled")
        self.assertEqual(turns[0]["cancel_result"], -125)
        self.assertEqual(turns[0]["queue_peak_bytes"], 4096)
        self.assertTrue(turns[0]["kws_rearm"])
        self.assertEqual(turns[0]["rearm_attempts"], 2)
        self.assertEqual(turns[0]["rearm_failures"], 1)
        self.assertIsNone(turns[0]["asr_final_ms"])
        self.assertEqual(turns[1]["result"], "incomplete")
        self.assertEqual(turns[1]["requests"]["asr"], 1)

    def test_voice_report_rejects_reused_turn_and_malformed_trace(self):
        log = self.voice_log(
            [
                (1000, "endpoint_end", 0, 0),
                (1500, "channel_idle", 0, 0),
                (2000, "endpoint_end", 0, 0),
            ]
        )
        with self.assertRaises(hil_test.HilTestError):
            hil_test.voice_report(log, boot_id="boot-a")
        with self.assertRaises(hil_test.HilTestError):
            hil_test.voice_report(
                "[voice-stage] request=7 mono_ms=broken", boot_id="boot-a"
            )

    def test_voice_report_text_input_and_queue_gaps_keep_evidence_scope(self):
        log = (
            self.voice_log(
                [
                    (1000, "plan_request", 1, 0),
                    (1300, "final_first", 3, 0),
                    (1500, "first_media", 2048, 0),
                    (2500, "queue_peak", 4096, 0),
                    (3000, "channel_idle", 0, 0),
                ]
            )
            + "\n[voice] TTS queue: pcm=16000 peak=4096 rebuffer=2 max_receive_gap_ms=300"
        )
        mark = dict(
            request_id=7,
            input_mode="text",
            input_end_mono_ms=900,
            clock_id="ap:boot-a",
            uncertainty_ms=0,
            input_evidence="accepted-text-001",
        )
        turn = hil_test.voice_report(log, boot_id="boot-a", input_marks=[mark])[
            "turns"
        ][0]
        self.assertEqual(turn["media_first_ms"], 600)
        self.assertEqual(turn["queue_peak_bytes"], 4096)
        self.assertIsNone(turn["total_ms"])
        self.assertIsNone(turn["pcm_max_gap_ms"])
        self.assertIsNone(turn["dac_underruns"])
        self.assertEqual(turn["latency_origin"], "text-submit")

        # Per-request queue observations also survive cancellation. They do
        # not measure network-only jitter or hardware DAC underruns.
        tagged = self.voice_log(
            [
                (1000, "input_text", 10, 0),
                (2000, "pcm_gap_max", 3432, -125),
                (2001, "queue_rebuffers", 1, -125),
                (2010, "channel_idle", 0, -125),
            ]
        )
        turn = hil_test.voice_report(tagged, boot_id="queue-cancel")["turns"][0]
        self.assertEqual(turn["pcm_max_gap_ms"], 3432)
        self.assertEqual(turn["queue_rebuffers"], 1)
        self.assertIsNone(turn["network_max_gap_ms"])
        self.assertIsNone(turn["dac_underruns"])

    def test_long_key_cli_waits_for_independent_terminal_power_evidence(self):
        class Client(FakeClient):
            def info(self):
                return {"major": 0, "minor": 7, "revision": 47, "build": 692}

            def status(self):
                return {"ready": True}

        @contextmanager
        def authorized(unused):
            yield Client()

        terminal = {
            "timeline": [
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 3, "error": -110},
            ],
            "final": {"contract_state": "FAILED", "raw_state": 3, "error": -110},
        }
        args = SimpleNamespace(
            operation="key",
            git_sha="1" * 40,
            session=5,
            held_ms=3000,
            pm_mode="pending",
            console_port="COM9",
            port="COM16",
        )
        factory = importlib.import_module("_lib.factory_diagnostics")
        with patch.object(
            workbench, "authorized_client", side_effect=authorized
        ), patch.object(
            factory, "wait_power", return_value=terminal, create=True
        ) as wait:
            result = hil_test.run(args, ["bk7258.py", "hil-test", "key"])

        wait.assert_called_once_with("COM9", timeout=35)
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["observation"]["final"]["contract_state"], "FAILED")

    def test_audio_client_uses_authenticated_kind20_without_external_media(self):
        wire = struct.pack(
            ">4s15I",
            b"BAS1",
            1,
            2,
            23,
            1,
            3,
            24576,
            0,
            0,
            0,
            (-125) & 0xFFFFFFFF,
            (-125) & 0xFFFFFFFF,
            0,
            0,
            0,
            0,
        )

        class Stub:
            engineering_audio_run = workbench.ControlClient.engineering_audio_run
            engineering_audio_status = workbench.ControlClient.engineering_audio_status

            def __init__(self):
                self.closed = False
                self.authenticated = True
                self._timeout = 1
                self.calls = []

            def _now(self):
                return 10

            def _exchange(self, command, payload, deadline):
                self.calls.append((command, bytes(payload), deadline))
                if command == 15:
                    offset = struct.unpack(">I", payload)[0] & 0xFFFF
                    return (64, *struct.unpack(">4I", wire[offset : offset + 16]))
                return (0, 0, 0, 0, 0)

            def close(self):
                self.closed = True

        client = Stub()
        accepted = client.engineering_audio_run(23)
        self.assertTrue(accepted["accepted"])
        self.assertEqual(struct.unpack(">II", client.calls[0][1]), (20, 32))
        self.assertEqual(
            hil_test.decode_audio_command(client.calls[1][1])["session"], 23
        )
        status = client.engineering_audio_status()
        self.assertEqual(status["state"], "complete")
        self.assertFalse(client.closed)

    def test_audio_run_is_fixed_and_status_covers_all_three_sessions(self):
        record = hil_test.encode_audio_run(session=17)
        self.assertEqual(len(record), 32)
        self.assertEqual(
            hil_test.decode_audio_command(record),
            {"operation": "run", "session": 17, "sequence": 1},
        )
        wire = struct.pack(
            ">4s15I",
            b"BAS1",
            1,
            2,
            17,
            1,
            3,
            24576,
            0,
            0,
            0,
            (-125) & 0xFFFFFFFF,
            (-125) & 0xFFFFFFFF,
            0,
            0,
            0,
            0,
        )
        status = hil_test.decode_audio_status(wire)
        self.assertEqual(status["state"], "complete")
        self.assertEqual(status["accepted_bytes"], 24576)
        self.assertEqual(status["cancel_write"], -125)
        self.assertEqual(status["cancel_drain"], -125)
        self.assertEqual(status["next_result"], 0)

    def test_audio_codec_rejects_caller_media_and_false_success(self):
        with self.assertRaises(hil_test.HilTestError):
            hil_test.encode_audio_run(session=0)
        with self.assertRaises(hil_test.HilTestError):
            hil_test.decode_audio_command(b"BKA1" + bytes(28))
        failed = struct.pack(
            ">4s15I",
            b"BAS1",
            1,
            3,
            9,
            1,
            1,
            8192,
            (-110) & 0xFFFFFFFF,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        )
        self.assertEqual(hil_test.decode_audio_status(failed)["result"], -110)

    def test_command_is_bounded_versioned_and_round_trips(self):
        record = hil_test.encode_command(
            "key", session=7, sequence=4, value=2, elapsed_ms=3000
        )
        self.assertEqual(len(record), 32)
        self.assertEqual(
            hil_test.decode_command(record),
            {
                "operation": "key",
                "session": 7,
                "sequence": 4,
                "key_mask": 2,
                "elapsed_ms": 3000,
                "flags": 0,
            },
        )

    def test_no_held_sequence_uses_real_command_path(self):
        client = FakeClient()
        result = hil_test.key_sequence(
            client, session=23, held_ms=3000, pm_mode="pending"
        )
        self.assertEqual(
            [item["operation"] for item in client.records],
            ["session", "key", "advance", "key"],
        )
        self.assertEqual(client.records[1]["key_mask"], 2)
        self.assertEqual(client.records[-1]["key_mask"], 0)
        self.assertEqual(result["sequence"], 4)

    def test_long_release_may_close_native_usb_before_ack_without_replay(self):
        class ClosingClient(FakeClient):
            def engineering_command(self, record):
                decoded = hil_test.decode_command(record)
                if decoded["operation"] == "key" and decoded["key_mask"] == 0:
                    self.records.append(decoded)
                    raise workbench.CommandUnconfirmed()
                return super().engineering_command(record)

        client = ClosingClient()
        result = hil_test.key_sequence(
            client, session=24, held_ms=3000, pm_mode="pending", query=False
        )
        self.assertEqual(
            [item["operation"] for item in client.records],
            ["session", "key", "advance", "key"],
        )
        self.assertIsNone(result["accepted"])
        self.assertEqual(result["final_command"], "unconfirmed")

        with self.assertRaises(workbench.CommandUnconfirmed):
            hil_test.key_sequence(
                ClosingClient(), session=25, held_ms=2999, pm_mode="blocked"
            )

    def test_status_requires_test_identity(self):
        client = FakeClient()
        client.snapshot["enabled"] = False
        with self.assertRaisesRegex(hil_test.HilTestError, "engineering test"):
            hil_test.require_status(client)

    def test_status_reports_retained_power_intent(self):
        wire = struct.pack(
            ">4s15I",
            b"BKS1",
            1,
            9,
            42,
            4,
            3000,
            0,
            4,
            1,
            0,
            0x102,
            0xFFFFFF8C,
            0,
            1,
            1,
            2,
        )
        status = hil_test.decode_status(wire)
        self.assertTrue(status["enabled"])
        self.assertTrue(status["power_intent"])
        self.assertEqual(status["pm_mode"], "late-ack")
        self.assertEqual(status["power_state"], "pending")
        self.assertTrue(status["power_unresolved"])
        self.assertEqual(status["power_error"], -116)
        self.assertEqual(status["voice_state"], "idle")
        self.assertEqual(status["storage_state"], "ready")
        self.assertEqual(status["network_state"], "ready")

    def test_json_result_keeps_identity_and_observation_layers(self):
        report = hil_test.report(
            test="k2_timeout",
            result="PASS",
            firmware={"version": "0.7.46+691", "build": 691},
            git_sha="1" * 40,
            command=["bk7258.py", "hil-test", "suite"],
            input_sequence=["down", "advance:3000", "up"],
            observation={"power_state": "failed", "power_error": -110},
        )
        encoded = json.dumps(report, sort_keys=True)
        self.assertIn('"git_sha": "1111111111111111111111111111111111111111"', encoded)
        self.assertEqual(report["test"], "k2_timeout")
        self.assertEqual(report["result"], "PASS")

    def test_key_result_requires_real_terminal_power_evidence(self):
        short = {
            "power_intent": False,
            "pm_requests": 0,
            "power_state": "idle",
            "power_error": 0,
        }
        self.assertEqual(hil_test.validate_key_result(2999, short)["result"], "PASS")

        long = {
            "timeline": [
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 3, "error": -110},
            ],
            "final": {"contract_state": "FAILED", "raw_state": 3, "error": -110},
        }
        self.assertEqual(hil_test.validate_key_result(3000, long)["result"], "PASS")
        with self.assertRaisesRegex(hil_test.HilTestError, "terminal"):
            hil_test.validate_key_result(
                3001,
                {
                    "timeline": [
                        {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258}
                    ],
                    "final": {
                        "contract_state": "WAIT_CP_OR_UNKNOWN",
                        "raw_state": 258,
                    },
                },
            )


if __name__ == "__main__":
    unittest.main()
