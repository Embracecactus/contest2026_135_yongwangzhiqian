# SPDX-License-Identifier: Apache-2.0
"""Bounded MiMo ASR -> chat -> streaming TTS; credentials stay on Gateway."""

from __future__ import annotations

import asyncio
import base64
import contextlib
import io
import json
import os
import ssl
import stat
import wave
from collections.abc import AsyncIterator
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit

import aiohttp

from .memory import MemorySession, MemoryStateError


MAX_INPUT_BYTES = 30 * 16_000 * 2
MAX_TEXT_CHARS = 4096
MAX_JSON_BYTES = 256 * 1024
MAX_SSE_BYTES = 256 * 1024
MAX_HISTORY_TURNS = 4
MAX_HISTORY_CHARS = 8192
_PERSONA_STYLES = {
    "gentle": "语气温柔、耐心，先理解对方的感受，再自然回应。",
    "playful": "语气轻快俏皮，适度幽默，不嘲讽对方。",
    "quiet": "语气平静克制，优先用一两句简短回应，避免连续追问。",
    "serious": "语气认真直接，表达清楚，建议具体，避免无关玩笑。",
    "tsundere_lite": "语气略带含蓄的俏皮嘴硬，但保持关心和尊重，不贬低或操控对方。",
}
PERSONA_MODES = tuple(_PERSONA_STYLES)


class ProviderError(Exception):
    """Only fixed, content-free error codes may cross the provider boundary."""


@dataclass(frozen=True)
class MiMoConfig:
    api_key: str = field(repr=False)
    tts_sample_rate: int
    base_url: str = "https://token-plan-cn.xiaomimimo.com/v1"
    chat_model: str = "mimo-v2.5"
    tts_voice: str = "mimo_default"
    request_timeout: float = 45.0
    persona_mode: str = "gentle"

    def __post_init__(self) -> None:
        if self.persona_mode not in PERSONA_MODES:
            raise ValueError("unsupported persona mode")
        url = urlsplit(self.base_url)
        if (url.scheme != "https" or not url.hostname or url.username is not None
                or url.password is not None or url.query or url.fragment
                or url.path.rstrip("/") != "/v1"):
            raise ValueError("MiMo requires a credential-free HTTPS /v1 base URL")
        if not self.api_key or len(self.api_key) > 4096 or any(
                ord(c) < 33 or ord(c) > 126 for c in self.api_key):
            raise ValueError("invalid MiMo credential")
        if self.tts_sample_rate not in (16000, 22050, 24000, 32000, 44100, 48000):
            raise ValueError("verified mono PCM16 sample rate is required")
        if self.chat_model not in ("mimo-v2.5", "mimo-v2.5-pro"):
            raise ValueError("unsupported chat model")
        if self.tts_voice not in (
                "mimo_default", "冰糖", "茉莉", "苏打", "白桦", "Mia", "Chloe", "Milo", "Dean"):
            raise ValueError("unsupported built-in voice")
        if not 1 <= self.request_timeout <= 90:
            raise ValueError("invalid request timeout")


def load_api_key(path: Path) -> str:
    """Read a bounded owner-only file without following a final symlink."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or stat.S_IMODE(info.st_mode) != 0o600 or info.st_size > 4096):
            raise ValueError("MiMo credential file must be owner-owned mode 0600")
        return os.read(fd, 4097).decode("ascii").strip()
    finally:
        os.close(fd)


def wav_audio(pcm: bytes) -> str:
    if not pcm or len(pcm) > MAX_INPUT_BYTES or len(pcm) % 2:
        raise ProviderError("invalid_input_audio")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(pcm)
    return "data:audio/wav;base64," + base64.b64encode(output.getvalue()).decode("ascii")


async def sse_data(chunks: AsyncIterator[bytes]) -> AsyncIterator[str]:
    """Parse bounded SSE records independent of HTTP/UTF-8 chunk boundaries."""
    pending = bytearray()
    data: list[bytes] = []
    size = 0
    async for chunk in chunks:
        pending.extend(chunk)
        while b"\n" in pending:
            line, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            line = line.rstrip(b"\r")
            size += len(line) + 1
            if size > MAX_SSE_BYTES:
                raise ProviderError("sse_record_too_large")
            if not line:
                if data:
                    try:
                        yield b"\n".join(data).decode("utf-8")
                    except UnicodeDecodeError:
                        raise ProviderError("invalid_sse_utf8") from None
                data = []
                size = 0
            elif line.startswith(b"data:"):
                value = line[5:]
                data.append(value[1:] if value.startswith(b" ") else value)
        if size + len(pending) > MAX_SSE_BYTES:
            raise ProviderError("sse_record_too_large")
    if pending or data:
        raise ProviderError("truncated_sse_record")


def _choice(response: object) -> dict:
    if not isinstance(response, dict) or response.get("error"):
        raise ProviderError("invalid_provider_response")
    choices = response.get("choices")
    if not isinstance(choices, list) or len(choices) != 1 or not isinstance(choices[0], dict):
        raise ProviderError("invalid_provider_choices")
    if choices[0].get("index") != 0:
        raise ProviderError("invalid_provider_choice_index")
    return choices[0]


def _text_response(response: object) -> str:
    choice = _choice(response)
    message = choice.get("message")
    if choice.get("finish_reason") != "stop" or not isinstance(message, dict):
        raise ProviderError("incomplete_provider_text")
    text = message.get("content")
    if (message.get("tool_calls") or not isinstance(text, str)
            or not text.strip() or len(text) > MAX_TEXT_CHARS):
        raise ProviderError("invalid_provider_text")
    return text.strip()


async def resample_pcm(chunks: AsyncIterator[bytes], sample_rate: int) -> AsyncIterator[bytes]:
    """Use FFmpeg's streaming resampler, keeping PCM out of files/argv/logs."""
    if sample_rate == 16000:
        async for chunk in chunks:
            yield chunk
        return
    process = await asyncio.create_subprocess_exec(
        "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
        "-probesize", "32", "-analyzeduration", "1",
        "-f", "s16le", "-ar", str(sample_rate), "-ac", "1", "-i", "pipe:0",
        "-f", "s16le", "-ar", "16000", "-ac", "1", "-flush_packets", "1", "pipe:1",
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL, limit=16384,
    )

    async def feed() -> None:
        try:
            async for chunk in chunks:
                process.stdin.write(chunk)
                await process.stdin.drain()
        finally:
            process.stdin.close()

    writer = asyncio.create_task(feed(), name="shaniu-tts-resample")
    try:
        while chunk := await process.stdout.read(4096):
            yield chunk
        await writer
        if await process.wait() != 0:
            raise ProviderError("resample_failed")
    finally:
        writer.cancel()
        if process.returncode is None:
            with contextlib.suppress(ProcessLookupError):
                process.kill()
        with contextlib.suppress(asyncio.CancelledError, Exception):
            await writer
        # A killed subprocess may still have unread pipe data. Drain it before
        # joining; Process.wait alone can hang on a paused stdout transport.
        await process.communicate()


class MiMoProvider:
    def __init__(self, config: MiMoConfig, *, tls_context=None) -> None:
        self.config = config
        # Tests may supply a CA context; disabling certificate checks is forbidden.
        if tls_context is not None and (
                not tls_context.check_hostname or tls_context.verify_mode != ssl.CERT_REQUIRED):
            raise ValueError("provider TLS must verify hostname and certificate")
        self._tls_context = tls_context

    def _session(self) -> aiohttp.ClientSession:
        return aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=self.config.request_timeout, connect=10, sock_read=15),
            headers={"Authorization": "Bearer " + self.config.api_key},
            trust_env=False, cookie_jar=aiohttp.DummyCookieJar(),
        )

    def _post(self, session: aiohttp.ClientSession, body: dict):
        return session.post(
            self.config.base_url.rstrip("/") + "/chat/completions", json=body,
            allow_redirects=False, ssl=self._tls_context, auto_decompress=True,
        )

    async def _json(self, session: aiohttp.ClientSession, body: dict) -> object:
        async with self._post(session, body) as response:
            if response.status != 200 or response.content_type != "application/json":
                raise ProviderError("provider_http_error")
            payload = bytearray()
            async for chunk in response.content.iter_chunked(8192):
                payload.extend(chunk)
                if len(payload) > MAX_JSON_BYTES:
                    raise ProviderError("provider_json_too_large")
            try:
                return json.loads(payload)
            except (ValueError, UnicodeError):
                raise ProviderError("invalid_provider_json") from None

    async def chat(self, text: str) -> str:
        if not isinstance(text, str) or not text.strip() or len(text) > MAX_TEXT_CHARS:
            raise ProviderError("invalid_input_text")
        async with self._session() as session:
            return await self._chat(session, text)

    async def _chat(self, session: aiohttp.ClientSession, text: str,
                    history: tuple[tuple[int, str, str], ...] = (),
                    persona_mode: str | None = None) -> str:
        persona_mode = self.config.persona_mode if persona_mode is None else persona_mode
        if persona_mode not in PERSONA_MODES:
            raise ProviderError("unsupported_persona_mode")
        messages = [{"role": "system", "content": "你是傻妞，一个虚构的 AI 伴侣。用简短中文自然回答，"
                     "不冒充真人。你的声音是合成声音。不能执行设备操作。"
                     + _PERSONA_STYLES[persona_mode]}]
        for _, user, assistant in history:
            messages.extend(({"role": "user", "content": user},
                             {"role": "assistant", "content": assistant}))
        messages.append({"role": "user", "content": text})
        return _text_response(await self._json(session, {
            "model": self.config.chat_model, "stream": False,
            "max_completion_tokens": 1024,
            "thinking": {"type": "disabled"},
            "messages": messages,
        }))

    async def _tts(self, session: aiohttp.ClientSession, text: str) -> AsyncIterator[bytes]:
        body = {"model": "mimo-v2.5-tts", "stream": True,
                "messages": [{"role": "assistant", "content": text}],
                "audio": {"format": "pcm16", "voice": self.config.tts_voice}}
        total = 0
        finished = False
        async with self._post(session, body) as response:
            if response.status != 200 or response.content_type != "text/event-stream":
                raise ProviderError("provider_http_error")
            async for record in sse_data(response.content.iter_chunked(8192)):
                if record == "[DONE]":
                    if not finished or not total or total % 2:
                        raise ProviderError("incomplete_tts")
                    return
                try:
                    obj = json.loads(record)
                except ValueError:
                    raise ProviderError("invalid_sse_json") from None
                if isinstance(obj, dict) and obj.get("choices") == [] and not obj.get("error"):
                    continue  # Optional usage-only record.
                choice = _choice(obj)
                delta = choice.get("delta")
                if finished or not isinstance(delta, dict):
                    raise ProviderError("invalid_tts_order")
                audio = delta.get("audio")
                if audio is not None:
                    encoded = audio.get("data") if isinstance(audio, dict) else None
                    if not isinstance(encoded, str):
                        raise ProviderError("invalid_tts_audio")
                    try:
                        chunk = base64.b64decode(encoded, validate=True)
                    except (ValueError, UnicodeError):
                        raise ProviderError("invalid_tts_base64") from None
                    total += len(chunk)
                    if total > 90 * self.config.tts_sample_rate * 2:
                        raise ProviderError("tts_too_long")
                    if chunk:
                        yield chunk
                reason = choice.get("finish_reason")
                if reason is not None:
                    if reason != "stop":
                        raise ProviderError("incomplete_tts")
                    finished = True
        raise ProviderError("truncated_tts")

    def new_conversation(self, memory: MemorySession | None = None) -> MiMoConversation:
        """Create one connection-scoped dialogue, optionally with private history."""
        return MiMoConversation(self, memory)


class MiMoConversation:
    """Context committed only after the device confirms completed playback."""

    def __init__(self, provider: MiMoProvider, memory: MemorySession | None = None) -> None:
        self._provider = provider
        self._persona_mode = provider.config.persona_mode
        self._memory = memory
        try:
            persisted = () if memory is None else memory.context()
        except MemoryStateError:
            # A broken optional store must never expose stale data or take down
            # the real-time voice path. Console permission queries still fail
            # closed until the operator repairs the store.
            self._memory = None
            persisted = ()
        self._history: list[tuple[int, str, str]] = [
            (0, transcript, reply) for transcript, reply in persisted
        ]
        self._pending: tuple[str, str] | None = None
        self._active = False
        self._closed = False

    @property
    def persona_mode(self) -> str:
        return self._persona_mode

    def set_persona(self, mode: str) -> None:
        """A trusted connection owner may change style between completed turns."""
        if self._closed or self._active or self._pending is not None:
            raise ProviderError("conversation_unavailable")
        if mode not in PERSONA_MODES:
            raise ProviderError("unsupported_persona_mode")
        self._persona_mode = mode

    def finish(self, turn_id: int, *, success: bool) -> None:
        if not success:
            self._pending = None
            # A device may report playback failure after TTS_END was sent.
            if self._history and self._history[-1][0] == turn_id:
                self._history.pop()
            return
        if self._closed or self._active or self._pending is None:
            raise ProviderError("invalid_conversation_commit")
        transcript, reply = self._pending
        self._history.append((turn_id, transcript, reply))
        self._pending = None
        while (len(self._history) > MAX_HISTORY_TURNS
               or sum(len(user) + len(reply) for _, user, reply in self._history) > MAX_HISTORY_CHARS):
            self._history.pop(0)
        if self._memory is not None:
            try:
                self._memory.append(transcript, reply)
            except MemoryStateError:
                # Continue with bounded volatile context; persistence is an
                # optional privacy feature and cannot break device playback.
                self._memory = None

    def clear_memory(self) -> None:
        """Forget loaded history after an authenticated delete or revocation."""
        if self._closed or self._active or self._pending is not None:
            raise ProviderError("conversation_unavailable")
        self._history.clear()

    def close(self) -> None:
        self._closed = True
        self._pending = None
        self._history.clear()

    async def reply(self, pcm: bytes) -> AsyncIterator[bytes]:
        if self._closed or self._active or self._pending is not None:
            raise ProviderError("conversation_unavailable")
        self._active = True
        try:
            async with contextlib.aclosing(self._generate(pcm)) as audio:
                async for chunk in audio:
                    yield chunk
        finally:
            self._active = False

    async def _generate(self, pcm: bytes) -> AsyncIterator[bytes]:
        provider = self._provider
        encoded = wav_audio(pcm)
        async with provider._session() as session:
            transcript = _text_response(await provider._json(session, {
                "model": "mimo-v2.5-asr", "stream": False,
                "messages": [{"role": "user", "content": [{"type": "input_audio",
                    "input_audio": {"data": encoded}}]}],
                "asr_options": {"language": "auto"},
            }))
            del encoded
            reply = await provider._chat(session, transcript, tuple(self._history), self._persona_mode)
            async with contextlib.aclosing(provider._tts(session, reply)) as audio:
                async with contextlib.aclosing(resample_pcm(audio, provider.config.tts_sample_rate)) as normalized:
                    async for chunk in normalized:
                        yield chunk
        if self._closed:
            raise ProviderError("conversation_closed")
        self._pending = (transcript, reply)
