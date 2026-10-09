// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

/** Owns one source from asynchronous preparation through Activity teardown.
 * Acceptance belongs to the source, not the shorter-lived BLE upload.
 * The UI owner calls [accept] and [close]; [publish] runs on the opening worker.
 */
internal class OtaSourceLease<T : AutoCloseable>(private val closeOnWorker: (T) -> Unit) {
    private var source: T? = null
    private var closed = false
    var accepted = false
        private set

    @Synchronized fun current(): T? = source

    /** A rejected handoff leaves cleanup with the worker that opened it. */
    @Synchronized fun publish(opened: T): Boolean {
        if (closed) return false
        check(source == null)
        source = opened
        return true
    }

    fun accept() {
        if (current() != null) accepted = true
    }

    fun shouldCloseAfterStatus(state: Long?, uploadPresent: Boolean): Boolean =
        accepted && (state == 3L || state == 0L && !uploadPresent)

    fun close() {
        val detached = synchronized(this) {
            closed = true
            source.also { source = null }
        }
        accepted = false
        // The UI detaches before shutting down its executor. A late publish
        // cannot enqueue cleanup there: it is refused and closed by its opener.
        detached?.let(closeOnWorker)
    }

    companion object {
        // Persisted verification may describe the preceding update while a
        // new source is preparing. Only recovery without a source, or this
        // source's own accepted update, may release the current start lease.
        fun mayApplyVerification(source: OtaSourceLease<*>?): Boolean = source?.accepted != false
    }
}
