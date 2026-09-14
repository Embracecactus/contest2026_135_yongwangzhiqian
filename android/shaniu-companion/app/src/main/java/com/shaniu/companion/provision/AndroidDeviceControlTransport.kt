// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.bluetooth.BluetoothDevice
import android.content.Context
import java.util.concurrent.Executor

/**
 * Bridges the pure session owner to the Android GATT implementation.  It owns
 * no UI object, does not scan, and deliberately has no reconnect policy: that
 * policy belongs to DeviceControlSession.
 */
internal class AndroidDeviceControlFactory(
    context: Context,
    private val device: BluetoothDevice,
    private val deviceId: String,
    private val executor: Executor,
    private val post: (() -> Unit) -> Unit,
) : DeviceControlSession.Factory {
    private val appContext = context.applicationContext

    override fun open(events: DeviceControlSession.Events): DeviceControlSession.Transport {
        return Transport(events)
    }

    private inner class Transport(private val events: DeviceControlSession.Events) :
        DeviceControlSession.Transport {
        private val lock = Any()
        private var closed = false
        private var connection: DeviceControlConnection? = null

        init {
            executor.execute {
                val created = try {
                    DeviceControlConnection(appContext, device, deviceId,
                        result = { command, snapshot -> post { events.result(command, snapshot) } },
                        onClosed = { reason -> post { events.closed(reason) } })
                } catch (_: Exception) {
                    post { events.closed("Control connection unavailable") }
                    return@execute
                }
                val closeNow = synchronized(lock) {
                    if (closed) true else { connection = created; false }
                }
                if (closeNow) created.close()
            }
        }

        override fun request(command: DeviceControlProtocol.Command, value: Int,
                             accepted: (Boolean) -> Unit) {
            executor.execute {
                val target = synchronized(lock) { if (closed) null else connection }
                if (target == null) post { accepted(false) }
                else target.request(command, value) { ok -> post { accepted(ok) } }
            }
        }

        override fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray,
                                accepted: (Boolean) -> Unit) {
            val owned = payload.copyOf()
            executor.execute {
                val target = synchronized(lock) { if (closed) null else connection }
                if (target == null) {
                    owned.fill(0)
                    post { accepted(false) }
                } else {
                    try {
                        target.requestOta(command, owned) { ok -> post { accepted(ok) } }
                    } finally {
                        owned.fill(0)
                    }
                }
            }
        }

        override fun close() {
            val target = synchronized(lock) {
                closed = true
                connection.also { connection = null }
            }
            if (target != null) executor.execute { target.close() }
        }
    }
}
