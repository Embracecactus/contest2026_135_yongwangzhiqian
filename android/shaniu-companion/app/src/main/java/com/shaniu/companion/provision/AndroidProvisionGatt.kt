// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.util.Log
import java.io.IOException
import java.util.UUID
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/** Caller must obtain CONNECT permission and select a device before constructing.
 * No scan, automatic claim, or credentials are sent by opening this transport.
 * Call start after the owner's callback/transport references are installed.
 * Events run on this connection's worker: copy plaintext before returning, do
 * not block, and post UI updates to the main thread. closed() runs exactly once.
 */
@SuppressLint("MissingPermission")
class AndroidProvisionGatt(
    context: Context,
    private val device: BluetoothDevice,
    tls: ProvisionTls,
    private val events: Events,
) : AutoCloseable {
    interface Events {
        fun tlsEstablished()
        fun plaintext(bytes: ByteArray)
        fun closed(reason: String)
        fun tick()
    }

    private class Event(val data: ByteArray?, val run: (ByteArray?) -> Unit) {
        fun clear() { data?.fill(0) }
    }

    private enum class Stage { CONNECTING, DISCOVERING, MTU, SUBSCRIBING, TLS }
    private val appContext = context.applicationContext
    private val queue = ArrayBlockingQueue<Event>(128)
    private val eventLock = Any()
    private val stopping = AtomicBoolean()
    private val started = AtomicBoolean()
    private val stopReason = AtomicReference("cancelled")
    private val inFlight = AtomicReference<ProvisionGattSession.Write?>()
    private var gatt: BluetoothGatt? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var stage = Stage.CONNECTING
    private var reportedReady = false
    private val transportStartedAtMs = System.nanoTime() / 1_000_000
    private val session = ProvisionGattSession(tls, ::deliverPlaintext)

    private fun deliverPlaintext(bytes: ByteArray) {
        if (stopping.get()) return
        if (!reportedReady) {
            reportedReady = true
            events.tlsEstablished()
        }
        if (!stopping.get()) events.plaintext(bytes)
    }

    private fun post(data: ByteArray? = null, action: (ByteArray?) -> Unit) {
        if (stopping.get()) return
        if (data != null && data.size !in 1..16384) { stop("invalid_packet"); return }
        val event = Event(data?.copyOf(), action)
        val accepted = synchronized(eventLock) { !stopping.get() && queue.offer(event) }
        if (!accepted) {
            event.clear()
            stop("event_queue_full")
        }
    }

    private fun current(candidate: BluetoothGatt): Boolean = candidate === gatt && !stopping.get()

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(candidate: BluetoothGatt, status: Int, newState: Int) {
            post {
                if (!current(candidate)) return@post
                if (status != BluetoothGatt.GATT_SUCCESS || newState == BluetoothProfile.STATE_DISCONNECTED) {
                    stopGatt("disconnected", status)
                } else if (newState == BluetoothProfile.STATE_CONNECTED && stage == Stage.CONNECTING) {
                    stage = Stage.DISCOVERING
                    if (!candidate.discoverServices()) stop("service_discovery_failed")
                }
            }
        }

        override fun onServicesDiscovered(candidate: BluetoothGatt, status: Int) {
            post {
                if (!current(candidate) || stage != Stage.DISCOVERING) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) { stopGatt("service_discovery_failed", status); return@post }
                val service = candidate.getService(SERVICE)
                tx = service?.getCharacteristic(TX)
                rx = service?.getCharacteristic(RX)
                if (tx == null || rx == null ||
                    tx!!.properties and BluetoothGattCharacteristic.PROPERTY_WRITE == 0 ||
                    rx!!.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY == 0 ||
                    rx!!.getDescriptor(CCC) == null) {
                    stop("provision_service_missing"); return@post
                }
                stage = Stage.MTU
                if (!candidate.requestMtu(185)) subscribe(candidate)
            }
        }

        override fun onMtuChanged(candidate: BluetoothGatt, mtu: Int, status: Int) {
            post {
                if (!current(candidate) || stage != Stage.MTU) return@post
                if (status == BluetoothGatt.GATT_SUCCESS) session.negotiatedMtu(session.generation, mtu)
                subscribe(candidate) // Failed negotiation keeps ATT MTU 23.
            }
        }

        override fun onDescriptorWrite(candidate: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            post {
                if (!current(candidate) || stage != Stage.SUBSCRIBING ||
                    descriptor.uuid != CCC || descriptor.characteristic !== rx) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) { stopGatt("subscription_failed", status); return@post }
                stage = Stage.TLS
                session.start()
            }
        }

        override fun onCharacteristicWrite(candidate: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            val pending = inFlight.get()
            post {
                if (!current(candidate) || characteristic !== tx || stage != Stage.TLS) return@post
                if (pending == null || inFlight.get() !== pending) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    stopGatt("gatt_write_failed", status)
                    return@post
                }
                session.writeCompleted(pending.generation, pending.token, status == BluetoothGatt.GATT_SUCCESS)
                inFlight.compareAndSet(pending, null)
            }
        }

        @Deprecated("Platform callback before API 33")
        override fun onCharacteristicChanged(candidate: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            notification(candidate, characteristic, characteristic.value ?: return)
        }

        override fun onCharacteristicChanged(candidate: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            notification(candidate, characteristic, value)
        }
    }

    private fun notification(candidate: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        if (characteristic.uuid != RX) return
        if (value.size !in 1..514) { stop("invalid_notification"); return }
        post(value) { copy ->
            if (!current(candidate) || characteristic !== rx || stage != Stage.TLS) return@post
            session.enqueueIncoming(session.generation, requireNotNull(copy))
            session.processInput()
        }
    }

    @Suppress("DEPRECATION")
    private fun subscribe(candidate: BluetoothGatt) {
        stage = Stage.SUBSCRIBING
        val characteristic = requireNotNull(rx)
        val descriptor = requireNotNull(characteristic.getDescriptor(CCC))
        if (!candidate.setCharacteristicNotification(characteristic, true)) {
            stop("notification_registration_failed"); return
        }
        val value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        val accepted = if (Build.VERSION.SDK_INT >= 33) candidate.writeDescriptor(descriptor, value) == 0
        else { descriptor.value = value; candidate.writeDescriptor(descriptor) }
        if (!accepted) stop("descriptor_write_start_failed")
    }

    @Suppress("DEPRECATION")
    private fun flushWrite() {
        if (stage != Stage.TLS || inFlight.get() != null) return
        val write = session.nextWrite() ?: return
        inFlight.set(write)
        val target = requireNotNull(tx)
        val connection = requireNotNull(gatt)
        val accepted = if (Build.VERSION.SDK_INT >= 33) {
            connection.writeCharacteristic(target, write.value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
        } else {
            target.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            target.value = write.value
            connection.writeCharacteristic(target)
        }
        if (!accepted) throw IOException("GATT write not accepted")
    }

    /** Only send authentication/config messages after the product claim owner
     * authorizes them. TLS establishment alone is not possession/local consent.
     */
    fun send(bytes: ByteArray) = post(bytes) { session.send(requireNotNull(it)) }

    /** Serialize product control requests with TLS receive and timeout events. */
    internal fun execute(action: () -> Unit) = post { action() }

    /** These are called by a decoded control response on this worker only. */
    internal fun promoteToControl() = session.promoteToControl()
    internal fun touchControlActivity() = session.touchControlActivity()

    private fun stop(reason: String, gattStatus: Int? = null) {
        synchronized(eventLock) {
            if (!stopping.get()) {
                stopReason.set(reason)
                stopping.set(true)
                if (reason != "cancelled") {
                    val status = gattStatus?.let { " gatt_status=$it" } ?: ""
                    Log.w(LOG_TAG, "transport_close reason=$reason stage=${stage.name.lowercase()} elapsed_ms=${elapsedMs()}$status")
                }
            }
        }
    }

    private fun stopGatt(reason: String, status: Int) = stop(reason, status)

    private fun elapsedMs(): Long = System.nanoTime() / 1_000_000 - transportStartedAtMs

    override fun close() = stop("cancelled")

    fun start() {
        check(started.compareAndSet(false, true)) { "Transport already started" }
        worker.start()
    }

    private val worker = Thread({
        try {
            // Do not register clients against a disabled/restarting adapter.
            // The control owner may retry later; provisioning keeps its
            // bounded transaction and neither path changes the user's radio.
            val adapter = appContext.getSystemService(BluetoothManager::class.java)?.adapter
            if (adapter?.isEnabled != true) stop("bluetooth_disabled")
            else gatt = device.connectGatt(appContext, false, callback, BluetoothDevice.TRANSPORT_LE)
                ?: throw IOException("GATT connect rejected")
            while (!stopping.get()) {
                val event = queue.poll(100, TimeUnit.MILLISECONDS)
                if (event != null) {
                    try { if (!stopping.get()) event.run(event.data) } finally { event.clear() }
                }
                session.tick()
                if (!stopping.get()) events.tick()
                if (session.closed) { stop(session.failure ?: "transport_closed"); continue }
                if (!stopping.get()) flushWrite()
                if (session.established && !reportedReady && !stopping.get()) {
                    reportedReady = true
                    events.tlsEstablished()
                }
            }
        } catch (_: SecurityException) { stop("bluetooth_permission_denied") }
        catch (_: DeviceControlProtocol.ControlTimeout) { stop("control_timeout") }
        catch (_: Exception) { stop(session.failure ?: "bluetooth_transport_failed") }
        finally {
            synchronized(eventLock) {
                stopping.set(true)
                while (true) { (queue.poll() ?: break).clear() }
            }
            session.close()
            inFlight.set(null)
            try { gatt?.disconnect() } catch (_: Exception) { }
            try { gatt?.close() } catch (_: Exception) { }
            gatt = null
            try { events.closed(stopReason.get()) } catch (_: Exception) { }
        }
    }, "shaniu-provision-gatt")

    companion object {
        private const val LOG_TAG = "ShaniuProvisionGatt"
        val SERVICE: UUID = UUID.fromString("81e70001-9b31-4c48-9c62-e6da4b392531")
        val TX: UUID = UUID.fromString("81e70002-9b31-4c48-9c62-e6da4b392531")
        val RX: UUID = UUID.fromString("81e70003-9b31-4c48-9c62-e6da4b392531")
        private val CCC: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
