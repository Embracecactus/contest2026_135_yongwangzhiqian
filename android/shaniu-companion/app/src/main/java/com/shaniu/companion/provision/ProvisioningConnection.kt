// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.util.Log

/** Device selection/CONNECT permission precede this owner. It copies bootstrap
 * proof and configuration; callers may clear their inputs after construction.
 * State callbacks run on the transport worker and must be posted to UI.
 */
class ProvisioningConnection internal constructor(
    context: Context,
    device: BluetoothDevice,
    bootstrap: ProvisionBootstrap,
    bundle: ByteArray,
    stateChanged: (ProvisionClaimProtocol.State) -> Unit,
    recover: Boolean = false,
    bindingStore: ProvisionBindingStore? = null,
    transportFactory: ((Transport.Events) -> Transport)? = null,
    recoveryTransactionOverride: ByteArray? = null,
) : AutoCloseable {
    /** Narrow transport boundary for instrumented protocol/UI acceptance.
     * Production always wraps AndroidProvisionGatt; tests provide an instance
     * scoped factory and still exercise ProvisionClaimProtocol and binding.
     */
    internal interface Transport : AutoCloseable {
        interface Events {
            fun tlsEstablished()
            fun plaintext(bytes: ByteArray)
            fun tick()
            fun closed(reason: String)
        }
        fun start()
        fun send(bytes: ByteArray)
    }

    private class AndroidTransport(
        context: Context,
        device: BluetoothDevice,
        tls: ProvisionTls,
        events: Transport.Events,
    ) : Transport {
        private val delegate = AndroidProvisionGatt(context, device, tls,
            object : AndroidProvisionGatt.Events {
                override fun tlsEstablished() = events.tlsEstablished()
                override fun plaintext(bytes: ByteArray) = events.plaintext(bytes)
                override fun tick() = events.tick()
                override fun closed(reason: String) = events.closed(reason)
            })

        override fun start() = delegate.start()
        override fun send(bytes: ByteArray) = delegate.send(bytes)
        override fun close() = delegate.close()
    }

    private val binding = ProvisionBindingTransaction(
        bindingStore ?: ProvisionBindingStore(context.applicationContext),
        bootstrap.deviceId,
        recover && recoveryTransactionOverride == null,
    )
    private val recoveryTransaction = recoveryTransactionOverride?.copyOf()
        ?: binding.recoveryTransaction()
    private val certificatePin = bootstrap.certificatePin()
    private val tls = bootstrap.newTls()
    private val protocol: ProvisionClaimProtocol =
        ProvisionClaimProtocol(bootstrap, bundle, ::send, { state ->
            stateChanged(if (recoveryTransactionOverride == null) binding.project(state) else state)
        }, binding::beforeApply, recoveryTransaction,
            beforeApplyConfiguration = { transaction, configuration ->
                ProvisionSettings.useControlKey(configuration) { key ->
                    binding.beforeApply(transaction, key, if (key == null) null else certificatePin)
                }
            }, ownerRebind = !bootstrap.screenBootstrap)
    private val transport: Transport = (transportFactory ?: { events ->
        AndroidTransport(context, device, tls, events)
    })(object : Transport.Events {
            override fun tlsEstablished() {
                tlsEstablished = true
                protocol.start()
            }
            override fun plaintext(bytes: ByteArray) = protocol.receive(bytes)
            override fun tick() { }
            override fun closed(reason: String) {
                val protocolState = protocol.state
                val failureStage = protocol.failureStage
                val failureCode = protocol.failureCode
                transportFailure = transportFailureFor(reason)
                Log.w(CLAIM_LOG_TAG, "claim_transport_closed category=$transportFailure " +
                    "tls_established=$tlsEstablished protocol_state=$protocolState " +
                    "failure_stage=${failureStage ?: "none"} failure_code=${failureCode ?: "none"}")
                try { protocol.disconnected() } finally { protocol.close() }
            }
        })
    @Volatile private var tlsEstablished = false
    @Volatile private var transportFailure: TransportFailure? = null

    private enum class TransportFailure { HANDSHAKE_TIMEOUT, CONNECTION_CLOSED, OTHER }

    private companion object {
        const val CLAIM_LOG_TAG = "ShaniuProvisionClaim"
    }

    // Starting only after all fields are assigned removes constructor/worker
    // races when a fast callback attempts to send authentication.
    init { transport.start() }
    private fun send(bytes: ByteArray): Unit = transport.send(bytes)

    /** A bounded, non-sensitive explanation for a terminal claim outcome. */
    fun failureMessage(): String? = when {
        protocol.state == ProvisionClaimProtocol.State.UNCONFIRMED -> null
        protocol.failureStage == ProvisionClaimProtocol.State.AUTHENTICATING && protocol.failureCode == -9 ->
            "设备认证失败（-9），请检查激活资料后重试。"
        protocol.failureStage == ProvisionClaimProtocol.State.VERIFYING && protocol.failureCode == -12 ->
            "设备验证配置失败（-12），请检查设置后重试。"
        transportFailure == TransportFailure.HANDSHAKE_TIMEOUT ->
            "设备安全连接超时，请保持设备靠近手机后重试。"
        protocol.failureCode != null -> "设备认领失败（${protocol.failureCode}），请检查设备状态后重试。"
        transportFailure == TransportFailure.CONNECTION_CLOSED ->
            "设备连接已断开，请确认设备通电并靠近手机后重试。"
        transportFailure == TransportFailure.OTHER ->
            "设备连接异常，请检查蓝牙和设备状态后重试。"
        protocol.failureStage != null -> "设备认领失败，请检查设备设置后重试。"
        else -> null
    }

    override fun close() = transport.close()

    private fun transportFailureFor(reason: String): TransportFailure = when (reason) {
        "handshake_timeout" -> TransportFailure.HANDSHAKE_TIMEOUT
        "disconnected", "transport_closed", "session_timeout", "write_timeout" -> TransportFailure.CONNECTION_CLOSED
        else -> TransportFailure.OTHER
    }
}
