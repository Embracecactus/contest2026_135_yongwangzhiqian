// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer

/** SDC1 over pinned TLS. All methods belong to the transport worker. A timeout
 * or malformed response closes this protocol; commands are never auto-replayed.
 */
internal class DeviceControlProtocol(
    key: ByteArray,
    private val send: (ByteArray) -> Unit,
    private val result: (Command, Snapshot) -> Unit,
    private val nowMs: () -> Long = { System.nanoTime() / 1_000_000 },
) : AutoCloseable {
    class ControlTimeout : IllegalStateException()

    enum class Command(val wire: Int) {
        AUTH(1), STATUS(2), CANCEL(3), VOLUME(4), PERSONA(5), CLEAR_HISTORY(6),
        MEMORY_SET(7), MEMORY_DELETE(8), INFO(9), OTA_BEGIN(10), OTA_APPEND(11),
        OTA_START(12), OTA_STATUS(13), OTA_CANCEL(14),
        CONFIG_READ(15), CONFIG_BEGIN(16), CONFIG_APPEND(17), CONFIG_APPLY(18), CONFIG_CANCEL(19),
    }
    data class FirmwareInfo(val major: Long, val minor: Long, val revision: Long,
                            val build: Long, val securityCounter: Long)
    data class OtaStatus(val state: Long?, val phase: Long?, val progress: Long?,
                         val total: Long?, val result: Int)
    data class ConfigChunk(val totalLength: Int, val bytes: ByteArray)
    data class Snapshot(val error: Int, val ready: Boolean, val busy: Boolean,
                        val volume: Int?, val persona: Int?, val turn: Int?, val runtimeError: Int?,
                        val memorySupported: Boolean = false, val memoryEnabled: Boolean? = null,
                        val memoryPending: Boolean = false, val memoryFailed: Boolean = false,
                        val wifiReady: Boolean? = null, val infoSupported: Boolean = false,
                        val firmwareInfo: FirmwareInfo? = null,
                        val otaSupported: Boolean = false, val otaStatus: OtaStatus? = null,
                        val publicConfigSupported: Boolean = false, val configChunk: ConfigChunk? = null)
    private val secret = key.copyOf().also { require(it.size == 32 && it.any { b -> b != 0.toByte() }) }
    private val input = ByteArray(40)
    private var used = 0
    private var sequence = 0
    private var pending: Command? = null
    private var deadline = 0L
    private var lastNow = nowMs()
    var authenticated = false
        private set
    var closed = false
        private set
    val busy: Boolean get() = pending != null

    /** Only called after the pinned TLS handshake succeeds. */
    fun start() {
        check(!closed && !authenticated && pending == null && sequence == 0)
        try { transmit(Command.AUTH, secret) } finally { secret.fill(0) }
    }

    fun request(command: Command, value: Int = 0): Boolean {
        check(!closed && authenticated)
        require(command != Command.AUTH && !command.isOta && !command.isConfig)
        require(when (command) {
            Command.VOLUME -> value in 0..100
            Command.PERSONA -> value in 0..4
            Command.MEMORY_SET -> value in 0..1
            else -> value == 0
        })
        if (pending != null) return false
        val payload = if (command == Command.VOLUME || command == Command.PERSONA || command == Command.MEMORY_SET)
            ByteBuffer.allocate(4).putInt(value).array() else byteArrayOf()
        try { transmit(command, payload) } finally { payload.fill(0) }
        return true
    }

    fun requestOta(command: Command, payload: ByteArray = byteArrayOf()): Boolean {
        check(!closed && authenticated)
        require(command.isOta)
        require(when (command) {
            Command.OTA_BEGIN -> payload.size == 4 &&
                ByteBuffer.wrap(payload).int.toUInt().toLong() in 44L..3371L
            Command.OTA_APPEND -> payload.size in 1..32
            Command.OTA_START, Command.OTA_STATUS, Command.OTA_CANCEL -> payload.isEmpty()
            else -> false
        })
        if (pending != null) return false
        val copy = payload.copyOf()
        try { transmit(command, copy) } finally { copy.fill(0) }
        return true
    }

    fun requestPayload(command: Command, payload: ByteArray): Boolean {
        check(!closed && authenticated)
        require(command.isConfig)
        require(when (command) {
            Command.CONFIG_READ -> (payload.size == 4 || payload.size == 20) && ByteBuffer.wrap(payload).let {
                val argument = it.int
                val kind = argument ushr 16; val offset = argument and 0xffff
                if (payload.size == 20) kind == RESET_TRANSFER_KIND && offset % 16 == 0
                else ((kind in 1..2 || kind == 5 || kind == 7 || kind == 8) && offset % 16 == 0) ||
                    ((kind == 4 || kind == 6 || kind == 0x7fff) && offset == 0) }
            Command.CONFIG_BEGIN -> payload.size == 8 && ByteBuffer.wrap(payload).let {
                val kind = it.int; val size = it.int
                when (kind) { 1 -> size in 15..393; 2 -> size in 137..65672; 3 -> size == 4; 4 -> size == 12; 5 -> size in 44..3371; 6 -> size == 12; 7 -> size in 52..9216; RESET_TRANSFER_KIND -> size == 32; else -> false } }
            Command.CONFIG_APPEND -> payload.size in 1..512
            Command.CONFIG_APPLY, Command.CONFIG_CANCEL -> payload.isEmpty()
            else -> false
        })
        if (pending != null) return false
        val copy = payload.copyOf()
        try { transmit(command, copy) } finally { copy.fill(0) }
        return true
    }

    private var pendingReadKind = 0

    private fun transmit(command: Command, payload: ByteArray) {
        if (command == Command.CONFIG_READ) pendingReadKind = ByteBuffer.wrap(payload).int ushr 16
        check(sequence < Int.MAX_VALUE)
        val frame = ByteBuffer.allocate(16 + payload.size).putInt(0x53444331)
            .putInt(command.wire).putInt(sequence).putInt(payload.size).put(payload).array()
        pending = command
        lastNow = nowMs(); deadline = lastNow + 10_000
        try { send(frame) } catch (_: Exception) {
            close(); throw IllegalStateException("Control request could not be sent")
        } finally { frame.fill(0) }
    }

    fun tick() {
        if (closed) return
        val now = nowMs()
        if (now < lastNow || (pending != null && now >= deadline)) {
            close(); throw ControlTimeout()
        }
        lastNow = now
    }

    fun receive(bytes: ByteArray) {
        check(!closed)
        try {
            tick()
            var offset = 0
            while (offset < bytes.size) {
                require(pending != null)
                val count = minOf(input.size - used, bytes.size - offset)
                bytes.copyInto(input, used, offset, offset + count)
                used += count; offset += count
                if (used == input.size) {
                    val frame = ByteBuffer.wrap(input)
                    val command = requireNotNull(pending)
                    require(frame.int == 0x53444331 && frame.int == (command.wire or Int.MIN_VALUE))
                    require(frame.int == sequence && frame.int == 24)
                    val error = frame.int
                    val flags = frame.int
                    val volume = frame.int; val persona = frame.int; val turn = frame.int; val runtimeError = frame.int
                    if (command.isOta) {
                        require(error <= 0)
                        val ota = OtaStatus(flags.unsignedOrNull(), volume.unsignedOrNull(),
                            persona.unsignedOrNull(), turn.unsignedOrNull(),
                            runtimeError)
                        val snapshot = Snapshot(error, false, false, null, null, null, null,
                            otaStatus = ota)
                        complete(command, snapshot)
                        continue
                    }
                    if (command == Command.INFO) {
                        require(error <= 0)
                        val info = if (error == 0) FirmwareInfo(
                            flags.toUInt().toLong(), volume.toUInt().toLong(),
                            persona.toUInt().toLong(), turn.toUInt().toLong(),
                            runtimeError.toUInt().toLong()) else null
                        val snapshot = Snapshot(error, false, false, null, null, null, null,
                            firmwareInfo = info)
                        complete(command, snapshot)
                        continue
                    }
                    if (command == Command.CONFIG_READ) {
                        require(error <= 0)
                        val chunk = if (error == 0) {
                            require(flags in 12..(when (pendingReadKind) { 7 -> 824; 8 -> 876; RESET_TRANSFER_KIND -> 28; else -> 393 }))
                            ConfigChunk(flags, input.copyOfRange(24, 40))
                        } else null
                        complete(command, Snapshot(error, false, false, null, null, null, null,
                            configChunk = chunk))
                        continue
                    }
                    require(error <= 0 && flags and 32767 == flags)
                    require(flags and 2048 == 0 || flags and 1024 != 0)
                    require(flags and 64 == 0 || flags and 32 != 0)
                    require(flags and 480 == 0 || flags and 512 != 0)
                    require(flags and 128 == 0 || (flags and 2 != 0 && flags and 352 == 0))
                    require(if (flags and 8 != 0) volume in 0..100 else volume == -1)
                    require(if (flags and 16 != 0) persona in 0..4 else persona == -1)
                    require(if (flags and 4 != 0) turn >= 0 else turn == -1 && runtimeError == 0)
                    require(error == 0 || (flags == 0 && runtimeError == 0))
                    if (command == Command.AUTH) {
                        require(error == 0 && flags == 0)
                        authenticated = true
                    }
                    val snapshot = Snapshot(error, flags and 1 != 0, flags and 2 != 0,
                        volume.takeIf { flags and 8 != 0 }, persona.takeIf { flags and 16 != 0 },
                        turn.takeIf { flags and 4 != 0 }, runtimeError.takeIf { flags and 4 != 0 },
                        flags and 512 != 0, (flags and 64 != 0).takeIf { flags and 32 != 0 },
                        flags and 128 != 0, flags and 256 != 0,
                        (flags and 2048 != 0).takeIf { flags and 1024 != 0 }, flags and 4096 != 0,
                        otaSupported = flags and 8192 != 0,
                        publicConfigSupported = flags and 16384 != 0)
                    complete(command, snapshot)
                }
            }
        } catch (error: ControlTimeout) {
            throw error
        } catch (_: Exception) {
            close(); throw IllegalStateException("Invalid control response")
        }
    }

    private fun complete(command: Command, snapshot: Snapshot) {
        input.fill(0); used = 0; pending = null; sequence++
        result(command, snapshot)
    }

    private val Command.isOta: Boolean
        get() = wire in Command.OTA_BEGIN.wire..Command.OTA_CANCEL.wire
    private val Command.isConfig: Boolean
        get() = wire in Command.CONFIG_READ.wire..Command.CONFIG_CANCEL.wire

    private fun Int.unsignedOrNull(): Long? =
        takeUnless { it == -1 }?.toUInt()?.toLong()

    companion object {
        /** Authenticated SDC1 config kind; never a claim or binding mutation. */
        const val RESET_TRANSFER_KIND = 9
    }

    override fun close() {
        secret.fill(0); input.fill(0); used = 0; pending = null
        authenticated = false; closed = true
    }
}
