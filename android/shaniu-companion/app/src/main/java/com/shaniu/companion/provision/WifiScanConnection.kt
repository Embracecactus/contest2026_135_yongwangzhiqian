// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.bluetooth.BluetoothDevice
import android.content.Context

/** A separate authenticated read-only connection for board-owned Wi-Fi scans. */
internal class WifiScanConnection(
    context: Context,
    device: BluetoothDevice,
    bootstrap: ProvisionBootstrap,
    result: (WifiScanProtocol.Result) -> Unit,
    closed: (String) -> Unit,
) : AutoCloseable {
    private val protocol: WifiScanProtocol = WifiScanProtocol(bootstrap, ::send, result)
    private val transport: AndroidProvisionGatt = AndroidProvisionGatt(context, device, bootstrap.newTls(),
        object : AndroidProvisionGatt.Events {
            override fun tlsEstablished() = protocol.start()
            override fun plaintext(bytes: ByteArray) = protocol.receive(bytes)
            override fun tick() = protocol.tick()
            override fun closed(reason: String) {
                protocol.close()
                closed(reason)
            }
        })

    init { transport.start() }
    private fun send(bytes: ByteArray) = transport.send(bytes)
    override fun close() = transport.close()
}
