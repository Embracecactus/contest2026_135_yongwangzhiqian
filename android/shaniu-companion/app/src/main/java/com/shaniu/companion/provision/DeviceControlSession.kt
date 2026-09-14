// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

/** One foreground control owner. Public methods and observers run on the owner
 * thread; transport callbacks are posted to it. Views never own a connection.
 * A temporary Activity stop has a bounded grace period, not a background service.
 */
internal class DeviceControlSession(
    private val nowMs: () -> Long,
    private val post: ((() -> Unit) -> Unit),
    private val schedule: (Long, () -> Unit) -> Cancel,
    private val pollCommand: () -> DeviceControlProtocol.Command = { DeviceControlProtocol.Command.STATUS },
) {
    interface Cancel { fun cancel() }
    interface Transport {
        fun request(command: DeviceControlProtocol.Command, value: Int, accepted: (Boolean) -> Unit)
        fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit)
        fun close()
    }
    interface Factory { fun open(events: Events): Transport }
    interface Events {
        fun result(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot)
        fun closed(reason: String)
    }
    enum class Connection { DISCONNECTED, CONNECTING, CONNECTED, RECONNECT_WAIT, SUSPENDED }
    data class State(
        val connection: Connection = Connection.DISCONNECTED,
        val authenticated: Boolean = false,
        val snapshot: DeviceControlProtocol.Snapshot? = null,
        val snapshotFresh: Boolean = false,
        val updatedAt: Long = 0,
        val readPending: Boolean = false,
        val writePending: Boolean = false,
        val error: String? = null,
        val operationMessage: String? = null,
        val firmwareInfo: DeviceControlProtocol.FirmwareInfo? = null,
        val generation: Long = 0,
    )
    private data class Request(val command: DeviceControlProtocol.Command, val value: Int = 0,
                               val payload: ByteArray? = null, val verification: Boolean = false) {
        val read get() = command == DeviceControlProtocol.Command.STATUS ||
            command == DeviceControlProtocol.Command.INFO || command == DeviceControlProtocol.Command.OTA_STATUS
        fun clear() { payload?.fill(0) }
    }
    private data class Confirmation(val command: DeviceControlProtocol.Command, val expected: Int?)
    private val observers = LinkedHashSet<(State) -> Unit>()
    private val resultObservers = LinkedHashSet<(DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot) -> Unit>()
    private var factory: Factory? = null
    private var transport: Transport? = null
    private var state = State()
    private var foreground = false
    private var userClosed = true
    private var reconnectAttempts = 0
    private var reconnect: Cancel? = null
    private var poll: Cancel? = null
    private var grace: Cancel? = null
    private var queued: Request? = null
    private var inFlight: Request? = null
    private var confirmation: Confirmation? = null
    private var requestToken = 0L
    private var infoNeeded = false

    fun observe(observer: (State) -> Unit): Cancel {
        observers += observer
        observer(state)
        return object : Cancel { override fun cancel() { observers -= observer } }
    }
    fun observeResults(observer: (DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot) -> Unit): Cancel {
        resultObservers += observer
        return object : Cancel { override fun cancel() { resultObservers -= observer } }
    }
    fun current(): State = state
    fun connect(factory: Factory) {
        disconnect()
        this.factory = factory
        userClosed = false
        reconnectAttempts = 0
        state = state.copy(snapshot = null, firmwareInfo = null, updatedAt = 0)
        open()
    }
    fun disconnect(user: Boolean = true) {
        /* A lifecycle grace disconnect must never turn an explicit user close
         * into an auto-reconnectable session. */
        if (user) userClosed = true
        closeTransport(if (user) Connection.DISCONNECTED else Connection.SUSPENDED)
    }
    fun setForeground(value: Boolean) {
        foreground = value
        grace?.cancel(); grace = null
        if (value) {
            if (transport == null && factory != null && !userClosed && reconnect == null) open()
            else armPoll()
        } else {
            poll?.cancel(); poll = null
            reconnect?.cancel(); reconnect = null
            grace = schedule(30_000) { post { if (!foreground) disconnect(user = false) } }
        }
    }
    fun request(command: DeviceControlProtocol.Command, value: Int = 0): Boolean {
        if (isOta(command) || command == DeviceControlProtocol.Command.AUTH) return false
        return enqueue(Request(command, value))
    }
    fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray = ByteArray(0)): Boolean {
        if (!isOta(command)) return false
        return enqueue(Request(command, payload = payload.copyOf()))
    }
    private fun enqueue(request: Request): Boolean {
        if (!foreground || !state.authenticated || transport == null || queued != null ||
            confirmation != null || inFlight?.read == false) {
            request.clear()
            return false
        }
        // Coalesce polling; a write can wait behind the one current read.
        if (request.read && inFlight != null) { request.clear(); return false }
        queued = request
        if (!request.read) publish(state.copy(operationMessage = "等待设备执行", error = null))
        pump()
        return true
    }
    private fun open() {
        val value = factory ?: return
        reconnect?.cancel(); reconnect = null
        closeTransport(Connection.CONNECTING)
        infoNeeded = false
        val generation = state.generation
        val events = object : Events {
            override fun result(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
                post { received(generation, command, snapshot) }
            }
            override fun closed(reason: String) { post { lost(generation, reason) } }
        }
        try { transport = value.open(events) }
        catch (_: Exception) { lost(generation, "connection_open_failed") }
    }
    private fun received(generation: Long, command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (generation != state.generation || transport == null) return
        val request = inFlight
        val initial = state.connection == Connection.CONNECTING && request == null && command == DeviceControlProtocol.Command.STATUS
        if (!initial && request?.command != command) return
        inFlight = null
        requestToken++
        request?.clear()
        var next = state.copy(connection = Connection.CONNECTED, authenticated = true)
        if (initial) infoNeeded = snapshot.infoSupported
        when (command) {
            DeviceControlProtocol.Command.STATUS -> {
                next = if (snapshot.error == 0) next.copy(snapshot = snapshot, snapshotFresh = true,
                    updatedAt = nowMs(), error = null) else next.copy(snapshotFresh = false,
                    error = "读取状态失败（${snapshot.error}），正在重试")
                if (request?.verification == true) {
                    val expected = confirmation
                    if (snapshot.error == 0) {
                        val confirmed = when (expected?.command) {
                            DeviceControlProtocol.Command.VOLUME -> snapshot.volume != null && snapshot.volume == expected.expected
                            DeviceControlProtocol.Command.PERSONA -> snapshot.persona != null && snapshot.persona == expected.expected
                            else -> true
                        }
                        next = next.copy(operationMessage = if (confirmed) "设备已回读确认" else "设备回读未确认设置，请重试")
                        confirmation = null
                    } else next = next.copy(operationMessage = "设置结果未确认，正在重新读取")
                }
            }
            DeviceControlProtocol.Command.INFO -> {
                if (snapshot.error == 0) next = next.copy(firmwareInfo = snapshot.firmwareInfo)
            }
            else -> if (!isOta(command)) {
                if (snapshot.error == 0) {
                    confirmation = Confirmation(command, when (command) {
                        DeviceControlProtocol.Command.VOLUME -> snapshot.volume ?: request?.value
                        DeviceControlProtocol.Command.PERSONA -> snapshot.persona ?: request?.value
                        else -> null
                    })
                    next = next.copy(operationMessage = "设备已接受，正在回读确认", error = null)
                } else {
                    confirmation = null
                    next = next.copy(operationMessage = operationError(snapshot.error))
                }
            }
        }
        if (initial || command == DeviceControlProtocol.Command.STATUS && snapshot.error == 0) reconnectAttempts = 0
        publish(next)
        // OTA upload may enqueue its next fragment here. It shares this same scheduler.
        resultObservers.toList().forEach { it(command, snapshot) }
        if (snapshot.error != 0 && request?.verification == true) armPoll()
        else pump()
    }
    private fun pump() {
        if (transport == null || !state.authenticated || inFlight != null) { publish(state); return }
        poll?.cancel(); poll = null
        val next = when {
            confirmation != null -> Request(DeviceControlProtocol.Command.STATUS, verification = true)
            queued != null -> queued.also { queued = null }
            infoNeeded -> { infoNeeded = false; Request(DeviceControlProtocol.Command.INFO) }
            else -> null
        }
        if (next == null) { publish(state); armPoll(); return }
        send(next)
    }
    private fun send(request: Request) {
        val target = transport ?: return
        val generation = state.generation
        val token = ++requestToken
        inFlight = request
        publish(state)
        val accepted: (Boolean) -> Unit = { ok -> post {
            if (generation == state.generation && token == requestToken && inFlight === request && !ok) {
                inFlight = null
                request.clear()
                if (!request.read) confirmation = null
                publish(state.copy(operationMessage = if (!request.read) "设备正在处理其他请求，请重试" else state.operationMessage))
                armPoll()
            }
        } }
        try {
            if (isOta(request.command)) target.requestOta(request.command, request.payload ?: ByteArray(0), accepted)
            else target.request(request.command, request.value, accepted)
        } catch (_: Exception) { lost(generation, "control_send_failed") }
    }
    private fun armPoll() {
        poll?.cancel(); poll = null
        if (!foreground || transport == null || !state.authenticated || inFlight != null) return
        val generation = state.generation
        poll = schedule(2_000) { post {
            poll = null
            if (generation != state.generation || !foreground || inFlight != null || transport == null) return@post
            if (confirmation != null || queued != null) pump()
            else send(Request(pollCommand()))
        } }
    }
    private fun lost(generation: Long, reason: String) {
        if (generation != state.generation) return
        closeTransport(Connection.DISCONNECTED)
        publish(state.copy(error = reason, operationMessage = "连接中断；未完成的操作不会自动重发"))
        if (!userClosed && foreground && factory != null) {
            val ticket = state.generation
            // A full firmware download can outlast the initial attempts.
            // Keep one foreground-only retry with a capped delay; background
            // and explicit disconnect cancel it through the same owner.
            val delay = (1_000L shl reconnectAttempts).coerceAtMost(30_000L)
            reconnectAttempts = (reconnectAttempts + 1).coerceAtMost(5)
            publish(state.copy(connection = Connection.RECONNECT_WAIT))
            reconnect = schedule(delay) { post {
                reconnect = null
                if (ticket == state.generation && !userClosed && foreground && transport == null) open()
            } }
        }
    }
    private fun closeTransport(connection: Connection) {
        poll?.cancel(); poll = null
        reconnect?.cancel(); reconnect = null
        grace?.cancel(); grace = null
        queued?.clear(); queued = null
        inFlight?.clear(); inFlight = null
        confirmation = null
        requestToken++
        val old = transport
        transport = null
        publish(state.copy(generation = state.generation + 1, connection = connection,
            authenticated = false, snapshotFresh = false, error = null, operationMessage = null))
        old?.close()
    }
    private fun publish(next: State) {
        state = next.copy(readPending = inFlight?.read == true,
            writePending = queued?.read == false || inFlight?.read == false || confirmation != null)
        observers.toList().forEach { it(state) }
    }
    companion object {
        fun operationError(error: Int): String = when (error) {
            -16 -> "设备正在收音、播报或处理设置，请结束后再试"
            -95 -> "设备固件暂不支持此操作"
            -11 -> "设备状态暂不可用，请稍后重试"
            -107 -> "设备语音服务暂未就绪"
            else -> "操作未完成（$error），已保留设备原值"
        }
        private fun isOta(command: DeviceControlProtocol.Command) = command.wire in 10..14
    }
}
