// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import android.util.Log
import java.util.concurrent.atomic.AtomicLong

/** Foreground, bounded discovery. A BLE name/address is only a selection hint;
 * the subsequent saved-certificate TLS check establishes actual identity.
 */
@SuppressLint("MissingPermission")
internal class DeviceControlScanner(context: Context,
    private val found: (BluetoothDevice, String) -> Unit,
    private val finished: (Boolean) -> Unit,
) : AutoCloseable {
    private val handler = Handler(Looper.getMainLooper())
    private val scanner = context.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner
    private val seen = mutableSetOf<String>()
    private var active = false
    private val scanId = nextScanId.incrementAndGet()
    private var startedAtMs = 0L
    private val timeout = Runnable { stop(false, "timeout") }
    private val callback = object : ScanCallback() {
        override fun onScanResult(type: Int, result: ScanResult) {
            handler.post {
                if (!active || seen.size >= 16 || !seen.add(result.device.address)) return@post
                if (seen.size == 1) Log.i(LOG_TAG, "scan=$scanId first_match")
                found(result.device, result.scanRecord?.deviceName?.take(40) ?: "附近的傻妞")
            }
        }
        override fun onScanFailed(errorCode: Int) {
            handler.post {
                Log.w(LOG_TAG, "scan=$scanId api_failed error_code=$errorCode")
                stop(true, "api_failed")
            }
        }
    }
    fun start() {
        check(!active)
        active = true
        startedAtMs = SystemClock.elapsedRealtime()
        Log.i(LOG_TAG, "scan=$scanId start")
        try {
            checkNotNull(scanner).startScan(listOf(ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(AndroidProvisionGatt.SERVICE)).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
            handler.postDelayed(timeout, 15_000)
        } catch (error: Exception) {
            Log.w(LOG_TAG, "scan=$scanId start_exception=${error.javaClass.simpleName}")
            stop(true, "start_exception")
        }
    }
    private fun stop(failed: Boolean, reason: String) {
        if (!active) return
        finish(reason)
        finished(failed)
    }
    override fun close() {
        finish("closed")
    }
    private fun finish(reason: String) {
        if (!active) return
        active = false
        handler.removeCallbacks(timeout)
        try { scanner?.stopScan(callback) } catch (error: Exception) {
            Log.w(LOG_TAG, "scan=$scanId stop_exception=${error.javaClass.simpleName}")
        }
        Log.i(LOG_TAG, "scan=$scanId end reason=$reason elapsed_ms=${SystemClock.elapsedRealtime() - startedAtMs} unique_matches=${seen.size}")
    }
    companion object {
        private const val LOG_TAG = "ShaniuBleScan"
        private val nextScanId = AtomicLong()
    }
}
