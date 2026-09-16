// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.nfc.cardemulation.HostApduService
import android.os.Bundle
import android.os.Handler
import android.os.Looper

class ShaniuHceService : HostApduService() {
    companion object {
        @Volatile var foregroundEnabled = false
        @Volatile var selectionListener: ((ByteArray) -> Unit)? = null
    }
    private val handler = Handler(Looper.getMainLooper())

    private val session = NfcHandoverSession()
    private var sessionListener: ((ByteArray) -> Unit)? = null

    override fun processCommandApdu(commandApdu: ByteArray?, extras: Bundle?): ByteArray {
        val listener = selectionListener
        if (sessionListener !== listener) { session.reset(); sessionListener = listener }
        val (response, locator) = session.exchange(commandApdu ?: byteArrayOf(), foregroundEnabled)
        if (listener != null && locator != null) {
            handler.post {
                // A late callback must not reach a resumed or replaced screen.
                if (foregroundEnabled && selectionListener === listener) listener(locator)
            }
        }
        return response
    }

    override fun onDeactivated(reason: Int) { session.reset() }
}
