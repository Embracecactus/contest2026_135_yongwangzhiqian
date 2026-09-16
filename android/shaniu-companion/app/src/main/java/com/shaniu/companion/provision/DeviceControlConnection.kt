// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.bluetooth.BluetoothDevice
import android.content.Context

/** Construct off the UI thread: Android Keystore/preferences are accessed here.
 * Caller selects BLE device and obtains permission, but only the saved pin and
 * owner key establish identity. Events run on the GATT worker; post UI changes.
 */
internal class DeviceControlConnection(
    context: Context,
    device: BluetoothDevice,
    deviceId: String,
    private val result: (DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot) -> Unit,
    onClosed: (String) -> Unit,
    pendingStore: ProvisionBindingStore? = null,
    pendingTransaction: String? = null,
) : AutoCloseable {
    private val credentials: Pair<ProvisionTls, DeviceControlProtocol> =
        (pendingStore ?: ProvisionBindingStore(context.applicationContext)).let { store ->
            val borrow: ((ByteArray, ByteArray) -> Pair<ProvisionTls, DeviceControlProtocol>) = { key, pin ->
            ProvisionTls(pin) to DeviceControlProtocol(key, ::send, ::received)
            }
            if (pendingStore == null) store.useControlIdentity(deviceId, borrow)
            else store.usePendingControlIdentity(deviceId, requireNotNull(pendingTransaction), borrow)
        }
    private val protocol: DeviceControlProtocol = credentials.second
    private val transport: AndroidProvisionGatt = try { AndroidProvisionGatt(context, device, credentials.first,
        object : AndroidProvisionGatt.Events {
            override fun tlsEstablished() = protocol.start()
            override fun plaintext(bytes: ByteArray) = protocol.receive(bytes)
            override fun tick() = protocol.tick()
            override fun closed(reason: String) {
                protocol.close()
                onClosed(reason)
            }
        }) } catch (error: Exception) { protocol.close(); throw error }

    init { transport.start() }
    private fun send(bytes: ByteArray): Unit = transport.send(bytes)
    private fun received(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.AUTH) {
            transport.promoteToControl()
            protocol.request(DeviceControlProtocol.Command.STATUS)
        }
        else {
            transport.touchControlActivity()
            result(command, snapshot)
        }
    }
    fun request(command: DeviceControlProtocol.Command, value: Int = 0,
                accepted: (Boolean) -> Unit = {}) = transport.execute {
        accepted(if (protocol.authenticated && !protocol.closed) protocol.request(command, value) else false)
    }
    /** Payload ownership crosses to the GATT worker; callers may scrub on return. */
    fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray = ByteArray(0),
                   accepted: (Boolean) -> Unit = {}) {
        val owned = payload.copyOf()
        transport.execute {
            try {
                accepted(if (protocol.authenticated && !protocol.closed)
                    protocol.requestOta(command, owned) else false)
            } finally {
                owned.fill(0)
            }
        }
    }
    fun requestPayload(command: DeviceControlProtocol.Command, payload: ByteArray,
                       accepted: (Boolean) -> Unit = {}) {
        val owned = payload.copyOf()
        transport.execute {
            try { accepted(if (protocol.authenticated && !protocol.closed) protocol.requestPayload(command, owned) else false) }
            finally { owned.fill(0) }
        }
    }
    override fun close() = transport.close()
}
