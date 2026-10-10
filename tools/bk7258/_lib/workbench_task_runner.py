# SPDX-License-Identifier: Apache-2.0
"""One explicitly selected child process and one authorized task; no replay."""
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import time
import uuid

from .workbench import ControlError

HEARTBEAT_SECONDS = 5.0
RUNNING_TTL_MS = 15000
TERMINAL_TTL_MS = 60000


@dataclass(frozen=True)
class Plan:
    command: tuple
    timeout: float
    progress_file: Path | None


def prepare(args):
    command = getattr(args, "task_command", None)
    timeout = getattr(args, "run_timeout", 3600.0)
    if (
        not command
        or len(command) > 128
        or any(not isinstance(s, str) or not s or "\0" in s for s in command)
        or sum(map(len, command)) > 16384
        or not math.isfinite(timeout)
        or not 0 < timeout <= 86400
    ):
        raise ValueError(
            "task-run requires an explicit argv and a finite 0..86400 s deadline"
        )
    for name in ("task_id", "event_sequence", "state", "ttl_ms", "progress"):
        if getattr(args, name, None) is not None:
            raise ValueError(
                "task-run owns the task ID, sequence and actual process state"
            )
    path = getattr(args, "progress_file", None)
    return Plan(tuple(command), timeout, path.absolute() if path else None)


def read_progress(path, task):
    """Read only an explicitly selected, bounded regular file; never scan."""
    if path is None:
        return None
    try:
        if path.is_symlink():
            return None
        fd = os.open(
            path,
            os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0),
        )
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > 256:
                return None
            data = json.loads(stream.read(257))
        value = data.get("progress")
        if data.get("task_id") == task and type(value) is int and 0 <= value <= 100:
            return value
    except (OSError, ValueError, AttributeError):
        pass
    return None


def _stop(process):
    if process.poll() is not None:
        return
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait(timeout=2)
    except ProcessLookupError:
        process.wait(timeout=2)


def perform(client, plan, *, cancel_requested=None):
    snapshot = client.task_status()
    if not snapshot["admitted"] or (
        snapshot["state"] in ("start", "progress") and not snapshot["expired"]
    ):
        raise ControlError("Device tasks are unavailable or another task is active")
    sequence = snapshot["event_sequence"]
    if sequence >= 2**64 - 20000:
        raise ControlError("Task sequence exhausted")
    task = uuid.uuid4().hex
    sequence += 1
    # A rejected or uncertain start must never launch the user program.
    reply = client.task_event(task, sequence, "start", RUNNING_TTL_MS, 0)
    if not reply.get("accepted"):
        raise ControlError("Task start was not accepted")
    process = None
    state, reason, code = "failure", "spawn_failed", None
    connected = True
    progress = None
    environment = os.environ.copy()
    environment["SHANIU_TASK_ID"] = task
    if plan.progress_file:
        environment["SHANIU_TASK_PROGRESS"] = str(plan.progress_file)
    started = time.monotonic()
    next_event = started + HEARTBEAT_SECONDS
    last = started
    try:
        process = subprocess.Popen(
            plan.command,
            shell=False,
            stdin=subprocess.DEVNULL,
            stdout=sys.stderr,
            stderr=sys.stderr,
            env=environment,
            start_new_session=os.name == "posix",
        )
        while process.poll() is None:
            now = time.monotonic()
            if (
                now < last
                or now - started >= plan.timeout
                or (cancel_requested is not None and cancel_requested())
            ):
                state = "canceled"
                reason = (
                    "clock_rollback"
                    if now < last
                    else ("timeout" if now - started >= plan.timeout else "requested")
                )
                _stop(process)
                break
            last = now
            if connected and now >= next_event:
                value = read_progress(plan.progress_file, task)
                if value is not None and (progress is None or value >= progress):
                    progress = value
                sequence += 1
                try:
                    reply = client.task_event(
                        task, sequence, "progress", RUNNING_TTL_MS, progress
                    )
                    connected = bool(reply.get("accepted"))
                except Exception:
                    connected = False
                # No catch-up burst and no reconnect/replay after uncertainty.
                next_event = time.monotonic() + HEARTBEAT_SECONDS
            time.sleep(0.05)
        code = process.wait(timeout=2)
        if state != "canceled":
            state = "success" if code == 0 else "failure"
            reason = "exited"
    except KeyboardInterrupt:
        state, reason = "canceled", "interrupted"
        if process is not None:
            _stop(process)
            code = process.returncode
    except OSError:
        if process is not None:
            _stop(process)
            code = process.returncode
        state, reason = "failure", "spawn_failed" if process is None else "host_error"
    finally:
        if process is not None and process.poll() is None:
            _stop(process)
    confirmed = False
    if connected:
        sequence += 1
        try:
            reply = client.task_event(
                task,
                sequence,
                state,
                TERMINAL_TTL_MS,
                100 if state == "success" else progress,
            )
            current = client.task_status()
            confirmed = bool(
                reply.get("accepted")
                and current["task_id"] == task
                and current["event_sequence"] == sequence
                and current["state"] == state
                and not current["expired"]
            )
        except Exception:
            pass
    return dict(
        task_id=task,
        event_sequence=sequence,
        process_state=state,
        process_returncode=code,
        reason=reason,
        device_result="confirmed" if confirmed else "unknown",
        feedback_verified=False,
    )
