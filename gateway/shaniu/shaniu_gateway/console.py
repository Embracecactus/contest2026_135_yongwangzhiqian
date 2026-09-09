# SPDX-License-Identifier: Apache-2.0
"""Authenticated console-v1 projections and controls for verified voice peers."""
from __future__ import annotations

import asyncio
import hashlib
import hmac
import json
import os
import re
import secrets
import sqlite3
import stat
import time
from dataclasses import dataclass, field
from pathlib import Path

from aiohttp import web, WSMsgType

from .devices import _unique_object, read_operator_registry
from .firmware import FirmwareRelease, FirmwareReleases
from .memory import MemoryStateError, MemoryStore
from .mimo import PERSONA_MODES, ProviderError
from .ota_transactions import (
    OtaTransaction,
    OtaTransactionError,
    OtaTransactionStore,
)
from .protocol import CAP_OTA, CAP_VOLUME, SessionState, ProtocolError
from websockets.exceptions import ConnectionClosed

_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9._:-]{0,127}', re.ASCII)
_TOKEN = re.compile(r'[A-Za-z0-9._~-]{32,256}', re.ASCII)
_SHA = re.compile(r'[0-9a-f]{64}', re.ASCII)
_FIELDS = {'protocol', 'kind', 'request_id', 'device_id', 'generation',
           'expected_revision', 'issued_at_ms', 'expires_at_ms', 'operation', 'arguments'}
_JSON_MEDIA_TYPES = {'application/json', 'application/vnd.shaniu.console-v1+json'}


def _now() -> int:
    return time.time_ns() // 1_000_000


class ConsoleStateError(RuntimeError):
    pass


@dataclass(frozen=True)
class ConsoleGrant:
    token_sha256: str
    device_id: str
    write: bool
    expires_at_ms: int

    def __post_init__(self):
        if (not isinstance(self.token_sha256, str) or not _SHA.fullmatch(self.token_sha256)
                or not isinstance(self.device_id, str) or not _ID.fullmatch(self.device_id)
                or type(self.write) is not bool or type(self.expires_at_ms) is not int
                or not 0 < self.expires_at_ms <= (1 << 63) - 1):
            raise ValueError('invalid console grant')


def load_console_grants(path: Path) -> tuple[ConsoleGrant, ...]:
    value = read_operator_registry(path)
    if (not isinstance(value, dict) or set(value) != {'format', 'grants'}
            or value['format'] != 'shaniu.console-access/1'
            or not isinstance(value['grants'], list) or not 1 <= len(value['grants']) <= 256):
        raise ValueError('invalid console access registry')
    grants = []
    for entry in value['grants']:
        if not isinstance(entry, dict) or set(entry) != {
                'token_sha256', 'device_id', 'write', 'expires_at_ms'}:
            raise ValueError('invalid console grant fields')
        grants.append(ConsoleGrant(**entry))
    if len({g.token_sha256 for g in grants}) != len(grants):
        raise ValueError('duplicate console token')
    return tuple(grants)


class ConsoleEpochs:
    """A persistent counter prevents pre-restart requests addressing a new peer."""
    def __init__(self, path: Path):
        fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
        with os.fdopen(fd, 'rb') as source:
            info = os.fstat(source.fileno())
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                    or info.st_mode & 0o077):
                raise ValueError('console state must be operator-private')
        self.db = sqlite3.connect(path)
        try:
            self.db.execute('CREATE TABLE IF NOT EXISTS console_epoch '
                            '(id INTEGER PRIMARY KEY CHECK(id=1), value INTEGER NOT NULL)')
            self.db.execute('INSERT OR IGNORE INTO console_epoch VALUES (1, 0)')
            self.db.execute('CREATE TABLE IF NOT EXISTS console_persona '
                            '(device_id TEXT PRIMARY KEY, mode TEXT NOT NULL)')
            self.db.commit()
        except Exception:
            self.db.close()
            raise

    def next(self) -> int:
        try:
            self.db.execute('BEGIN IMMEDIATE')
            row = self.db.execute('SELECT value FROM console_epoch WHERE id=1').fetchone()
            if row is None:
                raise ConsoleStateError('console epoch unavailable')
            value = row[0]
            if type(value) is not int or not 0 <= value < (1 << 63) - 1:
                raise ConsoleStateError('console epoch exhausted')
            self.db.execute('UPDATE console_epoch SET value=? WHERE id=1', (value + 1,))
            self.db.commit()
            return value + 1
        except Exception:
            self.db.rollback()
            raise

    def persona(self, device_id: str) -> str | None:
        if not isinstance(device_id, str) or not _ID.fullmatch(device_id):
            raise ValueError('invalid console device')
        try:
            row = self.db.execute(
                'SELECT mode FROM console_persona WHERE device_id=?', (device_id,),
            ).fetchone()
        except sqlite3.Error:
            raise
        if row is None:
            return None
        if len(row) != 1 or row[0] not in PERSONA_MODES:
            raise ConsoleStateError('invalid persisted persona')
        return row[0]

    def set_persona(self, device_id: str, mode: str) -> None:
        if (not isinstance(device_id, str) or not _ID.fullmatch(device_id)
                or mode not in PERSONA_MODES):
            raise ValueError('invalid console persona')
        try:
            self.db.execute('BEGIN IMMEDIATE')
            self.db.execute(
                'INSERT INTO console_persona(device_id, mode) VALUES(?, ?) '
                'ON CONFLICT(device_id) DO UPDATE SET mode=excluded.mode',
                (device_id, mode),
            )
            self.db.commit()
        except Exception:
            self.db.rollback()
            raise

    def close(self):
        self.db.close()


@dataclass
class _DeviceState:
    connection: object
    generation: int
    pending_persona: str | None = None
    revision: int = 0
    sequence: int = 0
    snapshot: dict | None = None
    receipts: dict = field(default_factory=dict)
    turn_id: int = 0
    ota_release: FirmwareRelease | None = None
    mutation_lock: asyncio.Lock = field(default_factory=asyncio.Lock)


class ConsoleService:
    """One event-loop owner; settings success means Gateway persona changed only."""
    def __init__(self, gateway, grants: tuple[ConsoleGrant, ...], state_path: Path,
                 firmware_releases: FirmwareReleases | None = None, *,
                 memory_store: MemoryStore | None = None,
                 ota_transactions: OtaTransactionStore | None = None):
        firmware_releases = firmware_releases or FirmwareReleases()
        if (gateway.device_bindings is None or not grants or len(grants) > 256
                or len({g.token_sha256 for g in grants}) != len(grants)
                or any(not gateway.device_bindings.contains_device(g.device_id) for g in grants)
                or any(not gateway.device_bindings.contains_device(device)
                       for device in firmware_releases.device_ids)):
            raise ValueError('console grants require registered devices')
        self.gateway = gateway
        self.grants = tuple(grants)
        self.firmware_releases = firmware_releases
        if memory_store is not None and not isinstance(memory_store, MemoryStore):
            raise ValueError('invalid memory store')
        self.memory_store = memory_store
        if ota_transactions is not None and not isinstance(
                ota_transactions, OtaTransactionStore):
            raise ValueError('invalid OTA transaction store')
        gateway_store = getattr(gateway, 'ota_transactions', None)
        if (ota_transactions is not None and gateway_store is not None
                and ota_transactions is not gateway_store):
            raise ValueError('Gateway and console OTA transaction stores differ')
        self.ota_transactions = ota_transactions or gateway_store
        if self.ota_transactions is not None:
            gateway.ota_transactions = self.ota_transactions
        gateway_releases = getattr(gateway, 'firmware_releases', FirmwareReleases())
        if gateway_releases.releases and gateway_releases.releases != firmware_releases.releases:
            raise ValueError('Gateway and console firmware releases differ')
        gateway.firmware_releases = firmware_releases
        self.epochs = ConsoleEpochs(state_path)
        self.states: dict[str, _DeviceState] = {}
        self.streams: dict[str, int] = {}
        self.websockets: set = set()
        self.app = web.Application(client_max_size=65536, middlewares=[self._guard])
        self.app.router.add_get('/console/v1/devices/{device}/snapshot', self.snapshot)
        self.app.router.add_get(
            '/console/v1/devices/{device}/firmware/releases', self.releases
        )
        self.app.router.add_post('/console/v1/devices/{device}/mutations', self.mutate)
        self.app.router.add_get('/console/v1/devices/{device}/events', self.events)
        self.app.on_cleanup.append(self._cleanup)
        self.app.on_shutdown.append(self._shutdown)
        self.app.on_response_prepare.append(self._no_store)

    async def _no_store(self, request, response):
        response.headers['Cache-Control'] = 'no-store'

    async def _shutdown(self, app):
        await asyncio.gather(*(ws.close(code=1001) for ws in tuple(self.websockets)))

    async def _cleanup(self, app):
        self.epochs.close()

    @web.middleware
    async def _guard(self, request, handler):
        if not request.secure:
            raise web.HTTPForbidden()
        if len(request.headers.getall('Authorization', [])) != 1:
            raise web.HTTPUnauthorized()
        authorization = request.headers.get('Authorization', '')
        token = authorization.removeprefix('Bearer ')
        if not authorization.startswith('Bearer ') or not _TOKEN.fullmatch(token):
            raise web.HTTPUnauthorized()
        digest = hashlib.sha256(token.encode('ascii')).hexdigest()
        grant = next((g for g in self.grants if hmac.compare_digest(g.token_sha256, digest)), None)
        if grant is None or _now() >= grant.expires_at_ms:
            raise web.HTTPUnauthorized()
        if request.match_info.get('device') != grant.device_id:
            raise web.HTTPForbidden()
        request['grant'] = grant
        try:
            response = await handler(request)
        except (ValueError, UnicodeError, RecursionError):
            raise web.HTTPBadRequest(text='invalid_console_request') from None
        except (sqlite3.Error, ConsoleStateError, MemoryStateError,
                OtaTransactionError):
            raise web.HTTPServiceUnavailable(text='console_state_unavailable') from None
        response.headers['Cache-Control'] = 'no-store'
        return response

    def _state(self, device):
        connection = self.gateway.device_connection(device)
        if connection is None:
            raise web.HTTPServiceUnavailable(text='device_offline')
        persona = getattr(connection.reply_session, 'persona_mode', None)
        if (persona not in PERSONA_MODES
                or not callable(getattr(connection.reply_session, 'set_persona', None))):
            raise web.HTTPServiceUnavailable(text='persona_provider_unavailable')
        state = self.states.get(device)
        if state is None or state.connection is not connection:
            persisted = self.epochs.persona(device)
            generation = self.epochs.next()
            state = self.states[device] = _DeviceState(
                connection, generation,
                pending_persona=(persisted if persisted != persona else None),
            )
            self._restore_ota_transaction(device, state)
        persona = self._restore_persona(state, persona)
        # A revision identifies the observed turn as well as preferences.
        # Count even turns not polled by the phone, so an old cancel cannot
        # accidentally address a newly started turn in the same generation.
        turn_id = connection.session.turn_id
        state.revision += turn_id - state.turn_id
        state.turn_id = turn_id
        return state, persona

    def _release_for_transaction(self, row: OtaTransaction) -> FirmwareRelease | None:
        return next((release for release in self.firmware_releases.releases
                     if release.device_id == row.device_id
                     and release.manifest_sha256 == row.manifest_sha256
                     and release.target_version == row.target_version), None)

    def _restore_ota_transaction(self, device: str, state: _DeviceState) -> None:
        if self.ota_transactions is None:
            return
        row = self.ota_transactions.load(device)
        if row is not None:
            state.ota_release = self._release_for_transaction(row)

    @staticmethod
    def _turn_is_idle(connection) -> bool:
        return (connection.session.state is SessionState.IDLE
                and not connection.playback_pending_turn
                and not connection.cancel_pending_turn
                and (connection._reply_task is None or connection._reply_task.done()))

    def _restore_persona(self, state: _DeviceState, current: str) -> str:
        desired = state.pending_persona
        if desired is None:
            return current
        if desired == current:
            state.pending_persona = None
            return current
        if not self._turn_is_idle(state.connection):
            return current
        try:
            state.connection.reply_session.set_persona(desired)
        except ProviderError:
            # The provider can still own an active or unconfirmed exchange even
            # when the transport has just returned to idle. Keep reporting the
            # live value and retry restoration on a later snapshot.
            return current
        state.pending_persona = None
        return desired

    def _snapshot(self, device):
        state, persona = self._state(device)
        device_status = state.connection.device_status
        memory_permission = 'not_granted'
        if self.memory_store is not None and self.memory_store.configured(device):
            memory_permission = ('allowed'
                                 if self.memory_store.permission(device) == 'enabled'
                                 else 'denied')
        phase = {SessionState.IDLE: 'idle', SessionState.UPLINK: 'listening',
                 SessionState.THINKING: 'thinking', SessionState.DOWNLINK: 'speaking'}
        update = dict(phase='unknown', target_version=None, progress_percent=0, error=None)
        release = state.ota_release
        row = (None if self.ota_transactions is None
               else self.ota_transactions.load(device))
        if (row is not None and release is not None
                and row.manifest_sha256 == release.manifest_sha256):
            ota_phase_name = {
                'dispatching': 'unknown', 'awaiting_first_report': 'unknown',
                'uncertain': 'unknown', 'downloading': 'downloading',
                'verifying': 'verifying', 'staged': 'staged',
                'rebooting': 'rebooting', 'trial': 'trial',
                'confirming': 'trial', 'rollback_check': 'rebooting',
                'confirmed': 'confirmed', 'rolled_back': 'rolled_back',
                'failed': 'failed', 'orphaned': 'failed',
            }[row.state]
            update = dict(phase=ota_phase_name, target_version=release.target_version,
                          progress_percent=row.progress_percent,
                          error=('update_device_error'
                                 if row.state in {'failed', 'orphaned'} else None))
        else:
            ota = state.connection.ota_status
            if ota is None or release is None or ota.manifest_sha256 != release.manifest_sha256:
                ota = None
        if row is None and ota is not None:
            ota_phase = {
                1: 'downloading', 2: 'verifying', 3: 'staged', 4: 'rebooting',
                5: 'trial', 6: 'confirmed', 7: 'rolled_back', 8: 'failed',
            }[ota.phase]
            update = dict(phase=ota_phase, target_version=release.target_version,
                          progress_percent=ota.progress_percent,
                          error=('update_device_error' if ota.result < 0 else None))
        payload = dict(claimed=True, presence='online', gateway='online',
                       battery_percent=(None if device_status is None else
                                        device_status.battery_percent),
                       charging=(None if device_status is None else
                                 device_status.charging),
                       firmware_version=(None if device_status is None else
                                         device_status.firmware_version),
                       revision=state.revision,
                       turn=('cancel_unconfirmed' if state.connection.cancel_confirmation_timed_out
                             else 'cancelling' if state.connection.cancel_pending_turn
                             else 'playback_unconfirmed' if state.connection.playback_confirmation_timed_out
                             else 'speaking' if state.connection.playback_pending_turn
                             else phase.get(state.connection.session.state, 'error')),
                       volume_percent=state.connection.volume_percent,
                       persona_mode=persona, emotion='unknown', permissions={
                           **{key: 'not_granted' for key in (
                               'microphone', 'camera', 'location', 'authorized_voice')},
                           'long_term_memory': memory_permission},
                       update=update)
        if state.snapshot is None or state.snapshot['payload'] != payload:
            state.sequence += 1
            state.snapshot = dict(protocol='console-v1', kind='event',
                                  event_id=f'snapshot-{state.generation}-{state.sequence}',
                                  device_id=device, generation=state.generation,
                                  sequence=state.sequence, occurred_at_ms=_now(),
                                  type='state.snapshot', payload=payload)
        return state.snapshot

    async def snapshot(self, request):
        if request.query:
            raise web.HTTPBadRequest()
        device = request.match_info['device']
        state, _ = self._state(device)
        if (state.connection.session.capabilities & CAP_VOLUME
                and state.connection.volume_percent is None):
            try:
                await state.connection.request_volume()
            except (ProtocolError, ConnectionClosed):
                pass  # Unknown remains null; never invent a default volume.
        if _now() >= request['grant'].expires_at_ms:
            raise web.HTTPUnauthorized()
        return web.json_response(self._snapshot(request.match_info['device']))

    async def releases(self, request):
        if (set(request.query) != {'generation'}
                or len(request.query.getall('generation')) != 1
                or re.fullmatch(r'[1-9][0-9]{0,18}',
                                request.query.get('generation', ''), re.ASCII) is None):
            raise web.HTTPBadRequest()
        device = request.match_info['device']
        state, _ = self._state(device)
        generation = int(request.query['generation'])
        if generation != state.generation:
            raise web.HTTPConflict(text='stale_generation')
        status = state.connection.device_status
        observed_version = None if status is None else status.firmware_version
        observed_root = None if status is None else status.firmware_root_sha256
        releases = self.firmware_releases.compatible(
            device, observed_version, observed_root)
        return web.json_response({
            'protocol': 'console-v1',
            'kind': 'firmware.release_catalog',
            'device_id': device,
            'generation': state.generation,
            'releases': [release.projection() for release in releases],
        })

    async def mutate(self, request):
        if not request['grant'].write:
            raise web.HTTPForbidden()
        if request.query or request.content_type not in _JSON_MEDIA_TYPES:
            raise web.HTTPBadRequest()
        value = json.loads(await request.read(), object_pairs_hook=_unique_object)
        if _now() >= request['grant'].expires_at_ms:
            raise web.HTTPUnauthorized()
        if (not isinstance(value, dict) or set(value) != _FIELDS
                or value['protocol'] != 'console-v1' or value['kind'] != 'mutation'
                or value['device_id'] != request.match_info['device']
                or not isinstance(value['request_id'], str) or not _ID.fullmatch(value['request_id'])
                or not isinstance(value['operation'], str) or not isinstance(value['arguments'], dict)):
            raise ValueError('invalid mutation')
        for key in ('generation', 'expected_revision', 'issued_at_ms', 'expires_at_ms'):
            if type(value[key]) is not int or not 0 <= value[key] <= (1 << 63) - 1:
                raise ValueError('invalid mutation integer')
        if value['generation'] == 0:
            raise ValueError('invalid generation')
        state, _ = self._state(value['device_id'])
        async with state.mutation_lock:
            current, _ = self._state(value['device_id'])
            if current is not state:
                raise web.HTTPConflict(text='device_reconnected')
            if _now() >= request['grant'].expires_at_ms:
                raise web.HTTPUnauthorized()
            return await self._mutate(value, state)

    async def _mutate(self, value, state):
        now = _now()
        canonical = hashlib.sha256(json.dumps(value, sort_keys=True,
                                              separators=(',', ':')).encode()).digest()
        state.receipts = {k: v for k, v in state.receipts.items() if v[2] > now}
        previous = state.receipts.get(value['request_id'])
        if previous is not None:
            if previous[0] != canonical:
                raise web.HTTPConflict(text='request_id_conflict')
            return web.json_response(previous[1])
        if len(state.receipts) >= 128:
            raise web.HTTPTooManyRequests()
        error = None
        if not (value['issued_at_ms'] <= now + 5000
                and value['issued_at_ms'] < value['expires_at_ms'] <= value['issued_at_ms'] + 60000
                and now < value['expires_at_ms']):
            error = 'request_expired'
        elif value['generation'] != state.generation:
            error = 'stale_generation'
        elif value['expected_revision'] != state.revision:
            error = 'revision_conflict'
        elif value['operation'] == 'turn.cancel':
            if value['arguments']:
                raise ValueError('invalid cancel arguments')
            if state.turn_id == 0:
                raise web.HTTPConflict(text='no_turn')
            else:
                try:
                    await state.connection.cancel_turn(state.turn_id)
                except ProtocolError as failure:
                    if failure.code == 'stale_turn':
                        error = 'revision_conflict'
                    elif failure.code == 'no_active_turn':
                        raise web.HTTPConflict(text='no_active_turn') from None
                    else:
                        raise web.HTTPServiceUnavailable(text='cancel_unavailable') from None
                except ConnectionClosed:
                    raise web.HTTPServiceUnavailable(text='device_offline') from None
                if error is None:
                    state.revision += 1
        elif value['operation'] == 'volume.set':
            if (set(value['arguments']) != {'volume_percent'}
                    or type(value['arguments']['volume_percent']) is not int
                    or not 0 <= value['arguments']['volume_percent'] <= 100):
                raise ValueError('invalid volume arguments')
            try:
                await state.connection.request_volume(value['arguments']['volume_percent'])
            except ProtocolError as failure:
                error = ('gateway_offline' if failure.code == 'device_offline'
                         else failure.code)
            except ConnectionClosed:
                error = 'gateway_offline'
            # A dispatched request can have taken effect even when confirmation
            # was lost. Invalidate the old revision and cache this receipt.
            state.revision += 1
        elif value['operation'] == 'firmware.update':
            manifest = value['arguments'].get('release_manifest_sha256')
            if (set(value['arguments']) != {'release_manifest_sha256'}
                    or not isinstance(manifest, str) or not _SHA.fullmatch(manifest)
                    or manifest in {'0' * 64, 'f' * 64}):
                raise ValueError('invalid firmware arguments')
            status = state.connection.device_status
            observed_version = None if status is None else status.firmware_version
            observed_root = None if status is None else status.firmware_root_sha256
            matches = tuple(release for release in self.firmware_releases.compatible(
                value['device_id'], observed_version, observed_root)
                if release.manifest_sha256 == manifest)
            store = self.ota_transactions
            persisted = None if store is None else store.load(value['device_id'])
            resume = None
            if persisted is not None and persisted.state not in {
                    'confirmed', 'rolled_back', 'failed', 'orphaned'}:
                if persisted.state != 'uncertain' or persisted.manifest_sha256 != manifest:
                    error = 'revision_conflict'
                else:
                    release = self._release_for_transaction(persisted)
                    if (release is None or status is None
                            or status.firmware_version not in {
                                release.required_source_version, release.target_version}
                            or status.firmware_root_sha256 !=
                               release.required_source_root_sha256):
                        store.advance(value['device_id'], persisted.transaction_id,
                                      'orphaned', persisted.progress_percent, 0, _now())
                        error = 'update_manifest_invalid'
                    else:
                        matches = (release,)
                        resume = persisted
            if error is not None:
                pass
            elif len(matches) != 1:
                error = 'update_manifest_invalid'
            elif not state.connection.session.capabilities & CAP_OTA:
                error = 'unsupported_operation'
            elif not self._turn_is_idle(state.connection):
                raise web.HTTPConflict(text='turn_in_progress')
            else:
                # Retain the verified release before dispatch.  A timeout can
                # race a late matching board report; that report remains a
                # read-only observable state rather than disappearing.
                state.ota_release = matches[0]
                transaction_id = None
                created = False
                if store is not None:
                    if resume is not None:
                        transaction_id = resume.transaction_id
                    else:
                        transaction_id = secrets.token_hex(16)
                        store.create(value['device_id'], transaction_id, manifest,
                                     matches[0].target_version, _now())
                        created = True

                def dispatched(boot: int, session: int, sequence: int) -> None:
                    if store is None or transaction_id is None:
                        return
                    if resume is None:
                        store.bind_dispatch(value['device_id'], transaction_id,
                                            boot, session, sequence, _now())
                    else:
                        store.redispatch(value['device_id'], transaction_id,
                                         boot, session, sequence, _now())

                try:
                    ota_status = await state.connection.request_ota(
                        manifest, transaction_id=transaction_id,
                        dispatched=dispatched if store is not None else None)
                except ProtocolError as failure:
                    if state.connection.ota_request_sent:
                        # The board may have acted despite a missing response.
                        state.revision += 1
                        error = 'update_device_error'
                    else:
                        state.ota_release = None
                        if created:
                            store.delete(value['device_id'])
                        error = ('gateway_offline' if failure.code == 'device_offline'
                                 else 'unsupported_operation' if failure.code == 'ota_not_supported'
                                 else 'revision_conflict' if failure.code == 'ota_busy'
                                 else 'gateway_offline')
                except ConnectionClosed:
                    if state.connection.ota_request_sent:
                        state.revision += 1
                        error = 'update_device_error'
                    else:
                        state.ota_release = None
                        if created:
                            store.delete(value['device_id'])
                        error = 'gateway_offline'
                else:
                    if ota_status.result < 0:
                        error = 'update_device_error'
                    state.revision += 1
        elif value['operation'] in {'permission.configure', 'memory.delete'}:
            device_id = value['device_id']
            if (self.memory_store is None
                    or not self.memory_store.configured(device_id)):
                error = 'unsupported_operation'
            elif value['operation'] == 'permission.configure':
                arguments = value['arguments']
                if (set(arguments) != {'capability', 'state'}
                        or arguments['capability'] not in {
                            'microphone', 'camera', 'location',
                            'long_term_memory', 'authorized_voice'}
                        or arguments['state'] not in {
                            'not_granted', 'allowed', 'denied'}):
                    raise ValueError('invalid permission arguments')
                if arguments['capability'] != 'long_term_memory':
                    error = 'unsupported_operation'
                elif arguments['state'] != 'denied':
                    error = 'local_confirmation_required'
                elif not self._turn_is_idle(state.connection):
                    raise web.HTTPConflict(text='turn_in_progress')
                else:
                    self.memory_store.revoke(device_id)
                    clear = getattr(state.connection.reply_session, 'clear_memory', None)
                    if not callable(clear):
                        raise ConsoleStateError('memory conversation unavailable')
                    clear()
                    state.revision += 1
            else:
                arguments = value['arguments']
                if (set(arguments) != {'scope'}
                        or arguments['scope'] not in {'conversations', 'all'}):
                    raise ValueError('invalid memory delete arguments')
                if not self._turn_is_idle(state.connection):
                    raise web.HTTPConflict(text='turn_in_progress')
                self.memory_store.delete(device_id, arguments['scope'])
                clear = getattr(state.connection.reply_session, 'clear_memory', None)
                if not callable(clear):
                    raise ConsoleStateError('memory conversation unavailable')
                clear()
                state.revision += 1
        elif value['operation'] != 'persona_mode.set':
            error = 'unsupported_operation'
        elif (set(value['arguments']) != {'persona_mode'}
              or value['arguments']['persona_mode'] not in PERSONA_MODES):
            raise ValueError('invalid persona arguments')
        elif not self._turn_is_idle(state.connection):
            raise web.HTTPConflict(text='turn_in_progress')
        else:
            previous_persona = state.connection.reply_session.persona_mode
            try:
                state.connection.reply_session.set_persona(value['arguments']['persona_mode'])
            except ProviderError:
                raise web.HTTPConflict(text='conversation_unavailable') from None
            try:
                self.epochs.set_persona(value['device_id'], value['arguments']['persona_mode'])
            except sqlite3.Error:
                try:
                    state.connection.reply_session.set_persona(previous_persona)
                except ProviderError:
                    raise ConsoleStateError('persona rollback unavailable') from None
                raise
            state.pending_persona = None
            state.revision += 1
        receipt = dict(protocol='console-v1', kind='mutation.receipt',
                       request_id=value['request_id'], device_id=value['device_id'],
                       generation=value['generation'], revision=state.revision,
                       status='accepted' if error is None else 'rejected', error=error)
        if value['expires_at_ms'] > now:
            state.receipts[value['request_id']] = (canonical, receipt,
                                                  min(value['expires_at_ms'], now + 60000))
        return web.json_response(receipt)

    async def events(self, request):
        if (set(request.query) != {'generation', 'after_sequence'}
                or any(len(request.query.getall(k)) != 1 for k in request.query)):
            raise web.HTTPBadRequest()
        generation = int(request.query['generation'])
        after = int(request.query['after_sequence'])
        if any(not re.fullmatch(r'0|[1-9][0-9]{0,18}', request.query[key], re.ASCII)
               for key in ('generation', 'after_sequence')):
            raise web.HTTPBadRequest()
        device = request.match_info['device']
        snapshot = self._snapshot(device)
        if generation != snapshot['generation'] or not 0 <= after <= snapshot['sequence']:
            raise web.HTTPConflict()
        if self.streams.get(device, 0) >= 4:
            raise web.HTTPTooManyRequests()
        ws = web.WebSocketResponse(heartbeat=20, max_msg_size=65536)
        ws.headers['Cache-Control'] = 'no-store'
        self.streams[device] = self.streams.get(device, 0) + 1
        try:
            await ws.prepare(request)
            self.websockets.add(ws)
            while not ws.closed and _now() < request['grant'].expires_at_ms:
                snapshot = self._snapshot(device)
                if snapshot['generation'] != generation:
                    break
                if snapshot['sequence'] > after:
                    await ws.send_json(snapshot)
                    after = snapshot['sequence']
                try:
                    message = await ws.receive(timeout=0.25)
                    if message.type not in (WSMsgType.PING, WSMsgType.PONG):
                        break
                except asyncio.TimeoutError:
                    pass
        except (web.HTTPException, ConnectionError, RuntimeError):
            pass
        finally:
            self.streams[device] -= 1
            self.websockets.discard(ws)
            if ws.prepared:
                await ws.close()
        return ws
