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
    private val timeout = Runnable { stop(false) }
    private val callback = object : ScanCallback() {
        override fun onScanResult(type: Int, result: ScanResult) {
            handler.post {
                if (!active || seen.size >= 16 || !seen.add(result.device.address)) return@post
                found(result.device, result.scanRecord?.deviceName?.take(40) ?: "附近的傻妞")
            }
        }
        override fun onScanFailed(errorCode: Int) { handler.post { stop(true) } }
    }
    fun start() {
        check(!active)
        active = true
        try {
            checkNotNull(scanner).startScan(listOf(ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(AndroidProvisionGatt.SERVICE)).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
            handler.postDelayed(timeout, 15_000)
        } catch (_: Exception) { stop(true) }
    }
    private fun stop(failed: Boolean) {
        if (!active) return
        close(); finished(failed)
    }
    override fun close() {
        if (!active) return
        active = false
        handler.removeCallbacks(timeout)
        try { scanner?.stopScan(callback) } catch (_: Exception) { }
    }
}
