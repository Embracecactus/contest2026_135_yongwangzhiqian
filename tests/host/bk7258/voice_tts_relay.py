#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded dialect-2 HTTPS TTS relay/replay fixture.

This is test infrastructure.  It deliberately has no device configuration
logic: the caller supplies a temporary server identity and points the device at
that endpoint for the one requested run.
"""
import contextlib
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse


MIMO_TTS_MODEL = "mimo-v2.5-tts"
MAX_BODY_LIMIT = 8 * 1024 * 1024
RELAY_READ_BYTES = 4096
MAX_TRACE_EVENTS = MAX_BODY_LIMIT // RELAY_READ_BYTES + 1
MIN_PREFIX_PCM_BYTES = 16384
MAX_PREFIX_PCM_BYTES = 2 * 24000 * 2
MODELS_BODY = (b'{"object":"list","data":[{"id":"mimo-v2.5-tts",'
               b'"object":"model"}]}')


class RelayError(ValueError):
    """Configuration or bounded protocol contract failure."""


class RelaySettings:
    def __init__(self, input_bytes, upstream_url, ca_file, key_file, response_file,
                 certificate, key, listen, port, report_dir, mode, max_body,
                 delay_ms, tts_voice):
        self.input_bytes = input_bytes
        self.input_text = input_bytes.decode("utf-8")
        self.text_sha256 = hashlib.sha256(input_bytes).hexdigest()
        self.upstream_url = upstream_url
        self.ca_file = ca_file
        self.key_file = key_file
        self.response_file = response_file
        self.certificate = certificate
        self.key = key
        self.listen = listen
        self.port = port
        self.report_dir = report_dir
        self.mode = mode
        self.max_body = max_body
        self.delay_ms = delay_ms
        self.tts_voice = tts_voice

    @classmethod
    def from_paths(cls, input_file, cloud_config, response_file, certificate, key,
                   listen, port, report_dir, mode, delay_ms=3000):
        input_file = Path(input_file)
        cloud_config = Path(cloud_config)
        response_file = Path(response_file)
        certificate = Path(certificate)
        key = Path(key)
        report_dir = Path(report_dir)
        if mode not in ("A", "B", "C"):
            raise RelayError("mode must be A, B, or C")
        if not input_file.is_file() or not certificate.is_file() or not key.is_file():
            raise RelayError("input, certificate, and key must exist")
        try:
            input_bytes = input_file.read_bytes()
            config = json.loads(cloud_config.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RelayError("invalid input or cloud config") from exc
        if not isinstance(config, dict):
            raise RelayError("cloud config must be an object")
        expected = config.get("text_sha256")
        if not isinstance(expected, str) or len(expected) != 64:
            raise RelayError("cloud config needs text_sha256")
        actual = hashlib.sha256(input_bytes).hexdigest()
        if not input_bytes or actual != expected.lower():
            raise RelayError("input text hash does not match frozen config")
        if config.get("tts_model") != MIMO_TTS_MODEL:
            raise RelayError("cloud config must select mimo-v2.5-tts")
        tts_voice = config.get("tts_voice")
        if not isinstance(tts_voice, str) or not tts_voice:
            raise RelayError("cloud config needs tts_voice")
        try:
            input_bytes.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RelayError("input must be UTF-8") from exc
        max_body = config.get("max_body", MAX_BODY_LIMIT)
        if not isinstance(max_body, int) or not 0 < max_body <= MAX_BODY_LIMIT:
            raise RelayError("max_body outside bounded limit")
        if not isinstance(delay_ms, int) or delay_ms < 0 or delay_ms > 120000:
            raise RelayError("delay outside bounded limit")
        key_file = config.get("key_file")
        if not isinstance(key_file, str) or not key_file:
            raise RelayError("cloud config needs key_file")
        key_file = Path(key_file)
        if not key_file.is_absolute():
            key_file = cloud_config.parent / key_file
        if mode == "A":
            upstream_url = config.get("upstream_url")
            parsed = urlparse(upstream_url) if isinstance(upstream_url, str) else None
            if (not parsed or parsed.scheme != "https" or not parsed.hostname or
                    parsed.username or parsed.password or parsed.query or parsed.fragment):
                raise RelayError("A needs a verified https upstream_url")
            if not key_file.is_file():
                raise RelayError("key_file must exist for A")
        else:
            upstream_url = None
        return cls(input_bytes, upstream_url, config.get("ca_file"), key_file,
                   response_file, certificate, key, listen, port, report_dir,
                   mode, max_body, delay_ms, tts_voice)


class RelayServer:
    """One TTS request per running server, with a TLS-only local endpoint."""
    def __init__(self, settings):
        self.settings = settings
        self.posts = 0
        self._lock = threading.Lock()
        handler = self._handler()
        self._http = ThreadingHTTPServer((settings.listen, settings.port), handler)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(settings.certificate, settings.key)
        self._http.socket = tls.wrap_socket(self._http.socket, server_side=True)
        self._thread = None

    @property
    def port(self):
        return self._http.server_address[1]

    def _handler(self):
        relay = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_args):
                # Never put request data or headers in host-test output.
                pass

            def _reply(self, status, body=b"", content_type="application/json"):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                if body:
                    self.wfile.write(body)

            def do_GET(self):
                if self.path == "/v1/models":
                    self._reply(200, MODELS_BODY)
                else:
                    self._reply(404)

            def do_POST(self):
                if self.path != "/v1/chat/completions":
                    self._reply(404)
                    return
                try:
                    size = int(self.headers.get("Content-Length", ""))
                except ValueError:
                    self._reply(400)
                    return
                if size <= 0 or size > relay.settings.max_body:
                    self._reply(413)
                    return
                body = self.rfile.read(size)
                if len(body) != size or not relay._valid_request(body):
                    self._reply(400)
                    return
                with relay._lock:
                    if relay.posts:
                        self._reply(429)
                        return
                    relay.posts = 1
                try:
                    if relay.settings.mode == "A":
                        source = relay._open_proxy(body)
                    else:
                        source = None
                        entity = relay._replay_entity()
                except (RelayError, OSError, ssl.SSLError, http.client.HTTPException):
                    self._reply(502)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Transfer-Encoding", "chunked")
                self.send_header("Connection", "close")
                self.end_headers()
                started_ns = time.monotonic_ns()
                try:
                    if source is not None:
                        entity, reads, writes = relay._stream_proxy(source, self.wfile,
                                                                      started_ns)
                        upstream_posts = 1
                    else:
                        reads = []
                        entity, writes = relay._stream_replay(entity, self.wfile, started_ns)
                        upstream_posts = 0
                    relay._finish_chunked(self.wfile, writes, started_ns)
                    relay._write_report(body, entity, upstream_posts, reads, writes)
                except (RelayError, OSError, ssl.SSLError, http.client.HTTPException):
                    # Headers have gone out.  Closing is the only safe failure signal.
                    return

        return Handler

    def _valid_request(self, body):
        try:
            root = json.loads(body.decode("utf-8"))
            messages = root["messages"]
            message = messages[0]
            return (isinstance(root, dict) and set(root) == {"model", "stream", "audio", "messages"}
                    and root["model"] == MIMO_TTS_MODEL and root["stream"] is True
                    and isinstance(root["audio"], dict)
                    and set(root["audio"]) == {"format", "voice"}
                    and root["audio"]["format"] == "pcm16"
                    and root["audio"]["voice"] == self.settings.tts_voice
                    and isinstance(messages, list) and len(messages) == 1
                    and isinstance(message, dict) and set(message) == {"role", "content"}
                    and message["role"] == "assistant"
                    and message["content"] == self.settings.input_text)
        except (KeyError, TypeError, UnicodeDecodeError, json.JSONDecodeError):
            return False

    def _replay_entity(self):
        try:
            entity = self.settings.response_file.read_bytes()
        except OSError as exc:
            raise RelayError("replay entity unavailable") from exc
        if not entity or len(entity) > self.settings.max_body:
            raise RelayError("replay entity outside bounded limit")
        return entity

    def _open_proxy(self, request_body):
        parsed = urlparse(self.settings.upstream_url)
        path = parsed.path.rstrip("/") + "/chat/completions"
        if not path.startswith("/"):
            path = "/chat/completions"
        token = self.settings.key_file.read_text(encoding="utf-8").strip()
        if not token:
            raise RelayError("empty key file")
        context = ssl.create_default_context(cafile=self.settings.ca_file)
        connection = http.client.HTTPSConnection(parsed.hostname, parsed.port or 443,
                                                 context=context, timeout=120)
        try:
            connection.request("POST", path, request_body, {
                "Content-Type": "application/json",
                "Authorization": "Bearer " + token,
                "Accept": "text/event-stream",
            })
            response = connection.getresponse()
        except Exception:
            connection.close()
            raise
        if response.status != 200:
            connection.close()
            raise RelayError("upstream did not return 200")
        return connection, response

    def _split(self, entity):
        """Return the first complete audio SSE boundary with safe prefill."""
        offset = 0
        pcm_bytes = 0
        while True:
            boundary = entity.find(b"\n\n", offset)
            if boundary < 0:
                break
            end = boundary + 2
            block = entity[offset:boundary]
            offset = end
            data = []
            for line in block.splitlines():
                if line.startswith(b"data:"):
                    data.append(line[5:].lstrip())
            if not data:
                continue
            try:
                event = json.loads(b"\n".join(data).decode("utf-8"))
                encoded = event["choices"][0]["delta"]["audio"]["data"]
            except (KeyError, IndexError, TypeError, UnicodeDecodeError,
                    json.JSONDecodeError):
                continue
            if not isinstance(encoded, str):
                raise RelayError("SSE audio data is not a string")
            try:
                pcm = base64.b64decode(encoded, validate=True)
            except (ValueError, UnicodeEncodeError) as exc:
                raise RelayError("invalid SSE audio data") from exc
            if not pcm or len(pcm) & 1:
                raise RelayError("invalid SSE PCM source bytes")
            pcm_bytes += len(pcm)
            if pcm_bytes > MAX_PREFIX_PCM_BYTES:
                raise RelayError("replay prefix exceeds two seconds")
            if pcm_bytes >= MIN_PREFIX_PCM_BYTES:
                return end, pcm_bytes
        raise RelayError("replay has no bounded audio SSE prefix")

    @staticmethod
    def _open_once(path):
        path.parent.mkdir(parents=True, exist_ok=True)
        return os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb")

    @staticmethod
    def _at(started_ns):
        return (time.monotonic_ns() - started_ns) // 1000000

    def _write_chunk(self, output, data, writes, started_ns):
        entered = self._at(started_ns)
        output.write(('%x\r\n' % len(data)).encode("ascii") + data + b"\r\n")
        output.flush()
        writes.append({"enter_ms": entered, "return_ms": self._at(started_ns),
                       "bytes": len(data)})

    def _finish_chunked(self, output, writes, started_ns):
        self._write_chunk(output, b"", writes, started_ns)

    def _stream_proxy(self, source, output, started_ns):
        connection, response = source
        entity = bytearray()
        reads = []
        writes = []
        try:
            with self._open_once(self.settings.response_file) as cache:
                while True:
                    if len(reads) >= MAX_TRACE_EVENTS:
                        raise RelayError("upstream event limit")
                    entered = self._at(started_ns)
                    # read1 returns bytes already available from a chunked stream;
                    # read(N) may wait for the requested size and erase the stimulus.
                    chunk = response.read1(RELAY_READ_BYTES)
                    reads.append({"enter_ms": entered, "return_ms": self._at(started_ns),
                                  "bytes": len(chunk)})
                    if not chunk:
                        break
                    if len(entity) + len(chunk) > self.settings.max_body:
                        raise RelayError("upstream entity outside bounded limit")
                    cache.write(chunk)
                    entity.extend(chunk)
                    self._write_chunk(output, chunk, writes, started_ns)
        except Exception:
            try:
                self.settings.response_file.unlink()
            except OSError:
                pass
            raise
        finally:
            connection.close()
        if not entity:
            raise RelayError("empty upstream entity")
        return bytes(entity), reads, writes

    def _stream_replay(self, entity, output, started_ns):
        writes = []
        if self.settings.mode == "B":
            split, _ = self._split(entity)
            self._write_chunk(output, entity[:split], writes, started_ns)
            time.sleep(self.settings.delay_ms / 1000.0)
            self._write_chunk(output, entity[split:], writes, started_ns)
        else:
            self._write_chunk(output, entity, writes, started_ns)
        return entity, writes

    def _write_report(self, request_body, entity, upstream_posts, reads, writes):
        self.settings.report_dir.mkdir(parents=True, exist_ok=True)
        payload = {
            "mode": self.settings.mode,
            "baseline": "relay-normal-network" if self.settings.mode == "A" else "replay",
            "request_sha256": hashlib.sha256(request_body).hexdigest(),
            "request_bytes": len(request_body),
            "input_sha256": self.settings.text_sha256,
            "response_sha256": hashlib.sha256(entity).hexdigest(),
            "response_bytes": len(entity),
            "upstream_posts": upstream_posts,
            "delay_ms": self.settings.delay_ms if self.settings.mode == "B" else 0,
            "prefix_bytes": self._split(entity)[0] if self.settings.mode == "B" else len(entity),
            "prefix_source_pcm_bytes": (self._split(entity)[1]
                                        if self.settings.mode == "B" else 0),
            "upstream_reads": reads,
            "downstream_writes": writes,
        }
        (self.settings.report_dir / ("tts-relay-" + self.settings.mode.lower() + ".json")) \
            .write_text(json.dumps(payload, sort_keys=True) + "\n", encoding="utf-8")

    @contextlib.contextmanager
    def running(self):
        self._thread = threading.Thread(target=self._http.serve_forever, daemon=True)
        self._thread.start()
        try:
            yield self
        finally:
            self._http.shutdown()
            self._http.server_close()
            self._thread.join(timeout=2)

    def serve_forever(self):
        try:
            self._http.serve_forever()
        finally:
            self._http.server_close()


def serve_cli(args):
    settings = RelaySettings.from_paths(args.input_file, args.cloud_config,
                                        args.response_file, args.cert, args.key,
                                        args.listen, args.port, args.report_dir,
                                        args.mode)
    RelayServer(settings).serve_forever()
