// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.content.Context
import android.content.SharedPreferences

/**
 * Durable public outcome of a provisioning transaction.
 *
 * Public locators and authenticated encrypted control keys share one atomic
 * commit. No bootstrap, Wi-Fi or cloud credentials are stored here. A binding
 * and its control key are published only when the matching receipt is consumed.
 */
internal class ProvisionBindingStore {
    internal interface Backend {
        fun contains(key: String): Boolean
        fun getString(key: String): String?

        /** Applies every entry atomically. A null value removes its key. */
        fun commit(changes: Map<String, String?>): Boolean
    }

    internal class SharedPreferencesBackend(private val preferences: SharedPreferences) : Backend {
        // Android publishes edits to memory before confirming disk persistence.
        // A failed commit makes every backend sharing that preferences object
        // untrustworthy until a new process reloads the durable file.
        private class State { var failed = false }
        private val state = synchronized(states) { states.getOrPut(preferences) { State() } }
        override fun contains(key: String): Boolean = synchronized(state) {
            checkHealthy(); preferences.contains(key)
        }
        override fun getString(key: String): String? = synchronized(state) {
            checkHealthy(); preferences.getString(key, null)
        }
        override fun commit(changes: Map<String, String?>): Boolean = synchronized(state) {
            checkHealthy()
            try {
                val edit = preferences.edit()
                changes.forEach { (key, value) ->
                    if (value == null) edit.remove(key) else edit.putString(key, value)
                }
                edit.commit().also { if (!it) state.failed = true }
            } catch (error: Exception) {
                state.failed = true
                throw error
            }
        }
        private fun checkHealthy() {
            check(!state.failed) { "Provisioning persistence is uncertain; restart the app" }
        }
        private companion object {
            val states = java.util.WeakHashMap<SharedPreferences, State>()
        }
    }

    private val backend: Backend
    private val cipher: ControlKeyCipher?

    constructor(context: Context) : this(
        SharedPreferencesBackend(context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)),
        ControlKeyCipher.android(),
    )

    internal constructor(backend: Backend, cipher: ControlKeyCipher? = null) {
        this.backend = backend
        this.cipher = cipher
    }

    @Synchronized fun boundDeviceId(): String? {
        if (!backend.contains(BOUND_DEVICE_ID)) return null
        val value = checkNotNull(backend.getString(BOUND_DEVICE_ID)) {
            "Corrupt bound-device state"
        }
        return checkNotNull(ProvisionBootstrap.validDeviceId(value)) {
            "Corrupt bound-device state"
        }
    }

    /** Reads the old receipt layout as a compatibility fallback. */
    @Synchronized fun pending(deviceId: String): String? {
        val device = ProvisionBootstrap.validDeviceId(deviceId) ?: return null
        val canonical = pendingKey(device)
        val key = when {
            backend.contains(canonical) -> canonical
            backend.contains(device) -> device
            else -> return null
        }
        val value = checkNotNull(backend.getString(key)) {
            "Corrupt provisioning receipt"
        }
        check(validTransaction(value)) { "Corrupt provisioning receipt" }
        return value
    }

    /** Must run before SPV1 APPLY is sent. */
    @Synchronized fun begin(deviceId: String, transaction: ByteArray,
                            controlKey: ByteArray? = null, certificatePin: String? = null): String {
        val device = validDevice(deviceId)
        require(certificatePin == null || (controlKey != null && PIN.matches(certificatePin)))
        require(transaction.size == TRANSACTION_BYTES)
        val hex = transaction.joinToString("") { "%02x".format(it.toInt() and 0xff) }
        check(pending(device) == null) { "A provisioning result needs reconciliation" }
        val sealed = controlKey?.let { requireNotNull(cipher).seal(scope(device, hex, certificatePin), it) }
        check(backend.commit(mapOf(pendingKey(device) to hex, device to null,
            pendingControl(device) to sealed,
            pendingPin(device) to certificatePin,
            controlRequired(device) to if (sealed == null) null else "1"))) {
            "Cannot save provisioning receipt"
        }
        return hex
    }

    /**
     * Publishes the bound device and consumes the matching pending receipt in
     * one preferences commit. On failure no binding is reported. The Android
     * backend stops reads and writes until process restart reloads disk state;
     * an in-memory result must never be mistaken for durable persistence.
     */
    @Synchronized fun commit(deviceId: String, transaction: String): Boolean {
        val device = validDevice(deviceId)
        if (!validTransaction(transaction) || pending(device) != transaction) return false
        val required = backend.contains(controlRequired(device))
        val sealed = backend.getString(pendingControl(device))
        val pin = backend.getString(pendingPin(device))
        if (required || sealed != null || pin != null) {
            if (!required || sealed == null || backend.getString(controlRequired(device)) != "1") return false
            try { requireNotNull(cipher).use(scope(device, transaction, pin), sealed) { } }
            catch (_: Exception) { return false }
        }
        return backend.commit(mapOf(
            BOUND_DEVICE_ID to device,
            BOUND_CONTROL to sealed,
            BOUND_PIN to pin,
            BOUND_TRANSACTION to if (sealed == null) null else transaction,
            pendingControl(device) to null,
            pendingPin(device) to null,
            controlRequired(device) to null,
            pendingKey(device) to null,
            device to null,
        ))
    }

    /** Borrowed plaintext is wiped on return, including when block throws. */
    @Synchronized fun <T> useControlKey(deviceId: String, block: (ByteArray) -> T): T =
        useBoundKey(deviceId, backend.getString(BOUND_PIN), block)

    private fun <T> useBoundKey(deviceId: String, pin: String?, block: (ByteArray) -> T): T {
        val device = validDevice(deviceId)
        check(boundDeviceId() == device) { "Device is not bound" }
        val transaction = requireNotNull(backend.getString(BOUND_TRANSACTION))
        check(validTransaction(transaction))
        return requireNotNull(cipher).use(scope(device, transaction, pin),
            requireNotNull(backend.getString(BOUND_CONTROL)), block)
    }

    /** Returns trust and proof together only after authenticating their binding.
     * Both arrays are borrowed for the callback and wiped afterwards.
     */
    @Synchronized fun <T> useControlIdentity(deviceId: String, block: (ByteArray, ByteArray) -> T): T {
        val pin = requireNotNull(backend.getString(BOUND_PIN)) { "Control trust is unavailable" }
        require(PIN.matches(pin))
        val bytes = ByteArray(32) { pin.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        // Authenticate the exact pin delivered to the caller, not a second
        // preferences read which another store instance could have changed.
        return try { useBoundKey(deviceId, pin) { key -> block(key, bytes) } }
        finally { bytes.fill(0) }
    }

    /** Borrow the exact pending control proof only for reconciliation. */
    @Synchronized fun <T> usePendingControlIdentity(deviceId: String, transaction: String,
                                                     block: (ByteArray, ByteArray) -> T): T {
        val device = validDevice(deviceId)
        require(validTransaction(transaction) && pending(device) == transaction)
        check(backend.getString(controlRequired(device)) == "1")
        val sealed = requireNotNull(backend.getString(pendingControl(device)))
        val pin = requireNotNull(backend.getString(pendingPin(device)))
        require(PIN.matches(pin))
        val bytes = ByteArray(32) { pin.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        return try {
            requireNotNull(cipher).use(scope(device, transaction, pin), sealed) { key -> block(key, bytes) }
        } finally { bytes.fill(0) }
    }

    /** A verified absence of a commit clears only its matching pending receipt. */
    @Synchronized fun notCommitted(deviceId: String, transaction: String): Boolean =
        clearMatching(deviceId, transaction)

    /** A verified pre-APPLY failure clears only its matching pending receipt. */
    @Synchronized fun confirmedFailure(deviceId: String, transaction: String): Boolean =
        clearMatching(deviceId, transaction)

    /** A disconnect after APPLY has no durable effect until it is reconciled. */
    @Synchronized fun unconfirmed(deviceId: String, transaction: String): Boolean {
        val device = ProvisionBootstrap.validDeviceId(deviceId) ?: return false
        return validTransaction(transaction) && pending(device) == transaction
    }

    /** Removes only this device's published binding; pending recovery remains. */
    @Synchronized fun clearBound(deviceId: String): Boolean {
        val device = ProvisionBootstrap.validDeviceId(deviceId) ?: return false
        if (boundDeviceId() != device) return false
        return backend.commit(mapOf(BOUND_DEVICE_ID to null,
            BOUND_CONTROL to null, BOUND_TRANSACTION to null, BOUND_PIN to null))
    }

    private fun clearMatching(deviceId: String, transaction: String): Boolean {
        val device = validDevice(deviceId)
        if (!validTransaction(transaction) || pending(device) != transaction) return false
        return backend.commit(mapOf(pendingKey(device) to null, device to null,
            pendingControl(device) to null, pendingPin(device) to null, controlRequired(device) to null))
    }

    private fun validDevice(value: String): String =
        requireNotNull(ProvisionBootstrap.validDeviceId(value)) { "Invalid device identifier" }

    private fun validTransaction(value: String): Boolean = TRANSACTION.matches(value)

    private fun pendingKey(deviceId: String) = "$PENDING_PREFIX$deviceId"
    private fun pendingControl(deviceId: String) = "@pending-control:$deviceId"
    private fun pendingPin(deviceId: String) = "@pending-pin:$deviceId"
    private fun controlRequired(deviceId: String) = "@control-required:$deviceId"
    private fun scope(deviceId: String, transaction: String, pin: String? = null): String {
        require(pin == null || PIN.matches(pin))
        return if (pin == null) "shaniu-control-v1\n$deviceId\n$transaction"
        else "shaniu-control-v2\n$deviceId\n$transaction\n$pin"
    }

    private companion object {
        const val PREFERENCES = "provision_receipts"
        /* '@' cannot start a valid public device locator, avoiding collisions
         * with the legacy layout that used a device ID directly as the key. */
        const val BOUND_DEVICE_ID = "@bound"
        const val BOUND_CONTROL = "@control"
        const val BOUND_PIN = "@control-pin"
        const val BOUND_TRANSACTION = "@control-transaction"
        const val PENDING_PREFIX = "@pending:"
        const val TRANSACTION_BYTES = 16
        val TRANSACTION = Regex("[0-9a-f]{32}")
        val PIN = Regex("[0-9a-f]{64}")
    }
}

/**
 * Projects protocol terminal states only after their durable local transition
 * succeeds. A board COMMITTED response therefore cannot escape as COMMITTED
 * while the handset still has only a pending receipt.
 */
internal class ProvisionBindingTransaction(
    private val store: ProvisionBindingStore,
    private val deviceId: String,
    private val recover: Boolean,
) {
    private var receipt: String? = store.pending(deviceId)

    init {
        check(recover || receipt == null) { "A provisioning result needs reconciliation" }
        if (recover) requireNotNull(receipt) { "No provisioning result to reconcile" }
    }

    fun recoveryTransaction(): ByteArray? = if (!recover) null else bytes(requireNotNull(receipt))

    fun beforeApply(transaction: ByteArray) = beforeApply(transaction, null)

    fun beforeApply(transaction: ByteArray, controlKey: ByteArray?, certificatePin: String? = null) {
        check(!recover && receipt == null)
        receipt = store.begin(deviceId, transaction, controlKey, certificatePin)
    }

    fun project(state: ProvisionClaimProtocol.State): ProvisionClaimProtocol.State = when (state) {
        ProvisionClaimProtocol.State.COMMITTED -> settle(state) { saved ->
            store.commit(deviceId, saved)
        }
        ProvisionClaimProtocol.State.NOT_COMMITTED -> settle(state) { saved ->
            store.notCommitted(deviceId, saved)
        }
        ProvisionClaimProtocol.State.FAILED -> {
            if (receipt == null) state
            else if (recover) ProvisionClaimProtocol.State.UNCONFIRMED
            else settle(state) { saved -> store.confirmedFailure(deviceId, saved) }
        }
        ProvisionClaimProtocol.State.UNCONFIRMED -> {
            receipt?.let { saved ->
                try { store.unconfirmed(deviceId, saved) } catch (_: Exception) { false }
            }
            state
        }
        else -> state
    }

    private fun settle(
        state: ProvisionClaimProtocol.State,
        transition: (String) -> Boolean,
    ): ProvisionClaimProtocol.State {
        val saved = receipt ?: return ProvisionClaimProtocol.State.UNCONFIRMED
        val persisted = try { transition(saved) } catch (_: Exception) { false }
        if (!persisted) return ProvisionClaimProtocol.State.UNCONFIRMED
        receipt = null
        return state
    }

    private fun bytes(hex: String): ByteArray = ByteArray(hex.length / 2) {
        hex.substring(it * 2, it * 2 + 2).toInt(16).toByte()
    }
}
