# SPDX-License-Identifier: Apache-2.0
"""Ephemeral loopback workbench. USB and credentials stay in the sole job owner."""
import argparse
import base64
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import secrets
import threading

from . import workbench, workbench_resources, workbench_selection, workbench_trial

ASSETS = Path(__file__).with_name("workbench_web")
LIMIT = 190000
READS = {
    "status",
    "info",
    "resource-status",
    "trial-status",
    "default-status",
    "task-status",
}
FIELDS = {
    **{name: set() for name in READS},
    "resource-upload": {"data", "ttl_ms"},
    "resource-resume": {"receipt_id", "data"},
    "resource-cancel": {"receipt_id"},
    "trial-start": {"expected_trial_id", "expression", "ttl_ms", "pack_filename"},
    "trial-cancel": {"expected_trial_id"},
    "default-refresh": {"selection_epoch", "expected_selection_id"},
    "default-set": {
        "selection_epoch",
        "expected_selection_id",
        "expected_default_revision",
        "pack_filename",
    },
    "default-cancel": {"selection_epoch", "expected_selection_id"},
    "default-recover": {"selection_epoch", "expected_selection_id"},
}
FIELDS["resource-status"] = {"receipt_id"}


class Conflict(ValueError):
    pass


def identity(value):
    if (
        not isinstance(value, str)
        or not re.fullmatch("[0-9a-f]{32}", value)
        or int(value, 16) == 0
    ):
        raise ValueError("Invalid local request identity")
    return value


def public(value):
    # JavaScript cannot represent all uint64 revisions/job IDs exactly.
    if type(value) is int and abs(value) > 2**53 - 1:
        return str(value)
    if isinstance(value, dict):
        return {k: public(v) for k, v in value.items()}
    if isinstance(value, list):
        return [public(v) for v in value]
    return value


class Service:
    def __init__(self, port, profile, directory, timeout):
        if not port or profile is None or not 0 < timeout <= 120:
            raise ValueError(
                "Explicit native port, protected profile and deadline required"
            )
        self.port, self.profile, self.timeout = port, profile, timeout
        self.directory = Path(directory)
        self.directory.mkdir(mode=0o700)  # Never reuse/erase earlier receipts.
        self.lock = threading.Lock()
        self.records = {}
        self.current = None
        self.worker = None
        self.cancel = threading.Event()
        self.closing = False

    def state(self):
        with self.lock:
            job = self.records.get(self.current)
            return json.loads(
                json.dumps(public({"job": job["public"] if job else None}))
            )

    def _arguments(self, operation, params, request_id):
        if (
            operation not in FIELDS
            or not isinstance(params, dict)
            or set(params) - FIELDS[operation]
        ):
            raise ValueError("Unsupported workbench action or fields")
        parser = argparse.ArgumentParser(add_help=False)
        workbench.add_arguments(parser)
        args = parser.parse_args([operation])
        args.port, args.profile, args.timeout = self.port, self.profile, self.timeout
        for key, value in params.items():
            if key not in ("data", "receipt_id"):
                if key == "expected_default_revision" and isinstance(value, str):
                    if not re.fullmatch("[0-9]{1,20}", value):
                        raise ValueError("Invalid revision")
                    value = int(value)
                setattr(args, key, value)
        if operation.startswith("trial-") and operation != "trial-status":
            args.operation_id = (
                request_id[:16] if int(request_id[:16], 16) else request_id[16:]
            )
        if operation.startswith("default-") and operation != "default-status":
            args.selection_nonce = request_id
        if operation.startswith("trial-"):
            workbench_trial.prepare(args)
        if operation.startswith("default-"):
            workbench_selection.prepare(args)
        if operation == "resource-upload":
            workbench_resources.encode(
                "begin", "01" * 16, "02" * 16, 0, 128, args.ttl_ms
            )
            args.receipt = self.directory / (request_id + ".json")
        if "receipt_id" in params:
            args.receipt = self.directory / (identity(params["receipt_id"]) + ".json")
            workbench_resources.read_receipt(args.receipt)
        if operation in ("resource-cancel", "resource-resume") and args.receipt is None:
            raise ValueError("Saved receipt required")
        if operation in ("resource-upload", "resource-resume"):
            encoded = params.get("data")
            if not isinstance(encoded, str) or len(encoded) > 175000:
                raise ValueError("Bounded eye pack required")
            data = base64.b64decode(encoded, validate=True)
            if not 128 <= len(data) <= workbench_resources.MAX_FILE:
                raise ValueError("Invalid eye pack length")
            args.file = self.directory / (request_id + ".bkep")
            with args.file.open("xb") as stream:
                stream.write(data)
        return args

    def submit(self, body):
        if not isinstance(body, dict) or set(body) != {"id", "operation", "params"}:
            raise ValueError("Invalid request")
        request_id = identity(body["id"])
        fingerprint = hashlib.sha256(
            json.dumps(body, sort_keys=True).encode()
        ).hexdigest()
        with self.lock:
            if request_id in self.records:
                if self.records[request_id]["fingerprint"] != fingerprint:
                    raise Conflict("Request identity already has different content")
                return {"id": request_id, "duplicate": True}
            if (
                self.closing
                or (self.worker and self.worker.is_alive())
                or len(self.records) >= 64
            ):
                raise Conflict("A job is active or this session is full")
            args = self._arguments(body["operation"], body["params"], request_id)
            self.cancel.clear()
            self.current = request_id
            self.records[request_id] = {
                "fingerprint": fingerprint,
                "public": {
                    "id": request_id,
                    "operation": args.operation,
                    "phase": "running",
                    "snapshot": None,
                    "result": None,
                    "cancel_requested": False,
                },
            }
            self.worker = threading.Thread(
                target=self._execute, args=(request_id, args), daemon=False
            )
            self.worker.start()
            return {"id": request_id, "duplicate": False}

    def _execute(self, request_id, args):
        def observe(value):
            with self.lock:
                self.records[request_id]["public"]["snapshot"] = public(value)

        try:
            result = workbench.run(
                args, observe=observe, cancel_requested=self.cancel.is_set
            )
            phase, error = "returned", None
        except Exception:
            result, phase = None, "unconfirmed"
            error = "操作未确认。保留回执，读取设备结果后再决定是否重试。"
        with self.lock:
            self.records[request_id]["public"].update(
                result=public(result), phase=phase, error=error
            )

    def request_cancel(self, body):
        if not isinstance(body, dict) or set(body) != {"id"}:
            raise ValueError("Invalid cancellation")
        request_id = identity(body["id"])
        with self.lock:
            if request_id != self.current:
                raise Conflict("Not the active job")
            job = self.records[request_id]["public"]
            if job["phase"] != "running" or job["operation"] not in (
                "resource-upload",
                "resource-resume",
                "resource-cancel",
            ):
                raise Conflict("No cancelable transfer running")
            self.cancel.set()
            job["cancel_requested"] = True
        return {"cancel_requested": True, "remote_cancellation_verified": False}

    def close(self):
        with self.lock:
            self.closing = True
            self.cancel.set()
            worker = self.worker
        if worker:
            worker.join(self.timeout + 12)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def log_message(self, *unused):
        pass  # No URLs, credential paths or exceptions in access logs.

    def reply(self, code, value, content_type="application/json; charset=utf-8"):
        data = (
            value
            if isinstance(value, bytes)
            else json.dumps(public(value), ensure_ascii=False).encode()
        )
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'",
        )
        self.end_headers()
        self.wfile.write(data)

    def authorized(self, api):
        expected = self.server.origin
        if (
            self.headers.get_all("Host") != [expected.removeprefix("http://")]
            or self.headers.get("Sec-Fetch-Site") in ("cross-site", "same-site")
            or self.headers.get("Origin") not in (None, expected)
        ):
            return False
        if api:
            headers = self.headers.get_all("Authorization")
            if (
                not headers
                or len(headers) != 1
                or not hmac.compare_digest(
                    headers[0].encode(), ("Bearer " + self.server.token).encode()
                )
            ):
                return False
        return self.command != "POST" or self.headers.get_all("Origin") == [expected]

    def do_GET(self):
        if not self.authorized(self.path.startswith("/api/")):
            return self.reply(403, {"error": "Local workbench authorization required"})
        if self.path == "/api/state":
            return self.reply(200, self.server.service.state())
        assets = {
            "/": ("index.html", "text/html"),
            "/app.js": ("app.js", "text/javascript"),
            "/style.css": ("style.css", "text/css"),
        }
        if self.path in assets:
            name, kind = assets[self.path]
            return self.reply(
                200, (ASSETS / name).read_bytes(), kind + "; charset=utf-8"
            )
        self.reply(404, {"error": "Unknown path"})

    def do_POST(self):
        if not self.authorized(True):
            return self.reply(403, {"error": "Local workbench authorization required"})
        lengths = self.headers.get_all("Content-Length") or []
        if (
            len(lengths) != 1
            or not re.fullmatch("[0-9]{1,6}", lengths[0])
            or self.headers.get("Transfer-Encoding") is not None
            or self.headers.get("Content-Type") != "application/json"
        ):
            return self.reply(400, {"error": "Invalid request framing"})
        size = int(lengths[0])
        if size > LIMIT:
            return self.reply(413, {"error": "Request too large"})
        try:
            raw = self.rfile.read(size)
            if len(raw) != size:
                raise ValueError()
            body = json.loads(raw)
            if self.path == "/api/start":
                result = self.server.service.submit(body)
            elif self.path == "/api/cancel":
                result = self.server.service.request_cancel(body)
            else:
                return self.reply(404, {"error": "Unknown path"})
            self.reply(202, result)
        except Conflict:
            self.reply(
                409, {"error": "当前任务未结束，或请求标识已使用。请读取任务状态。"}
            )
        except (ValueError, TypeError, OSError):
            self.reply(400, {"error": "输入无效或回执不可用；没有受理新的设备任务。"})


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, service, port):
        self.service = service
        self.token = secrets.token_urlsafe(32)
        self.slots = threading.BoundedSemaphore(8)
        super().__init__(("127.0.0.1", port), Handler)
        self.origin = f"http://127.0.0.1:{self.server_port}"

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def serve(args):
    if args.workbench_dir is None or not 0 <= args.listen_port <= 65535:
        raise ValueError("New workbench directory and valid loopback port required")
    if any(
        getattr(args, x, None) is not None
        for x in ("certificate", "certificate_sha256", "pc_key_file")
    ):
        raise ValueError("Browser workbench requires a protected PC profile")
    service = Service(args.port, args.profile, args.workbench_dir, args.timeout)
    server = None
    try:
        server = Server(service, args.listen_port)
        print(
            "打开本机工作台（关闭终端结束）：" + server.origin + "/#" + server.token,
            flush=True,
        )
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if server:
            server.server_close()
        service.close()
    return {"stopped": True, "receipts_preserved": True}
