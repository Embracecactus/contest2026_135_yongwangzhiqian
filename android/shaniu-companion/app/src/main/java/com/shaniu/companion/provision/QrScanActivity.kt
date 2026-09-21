// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.SurfaceTexture
import android.hardware.Camera
import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import java.io.IOException

/** Foreground QR reader for the device's first-use claim code.
 *
 * The camera only feeds the local decoder; no frame, image or decoded secret is
 * written to disk, logged or uploaded. The caller receives the raw payload text
 * and owns wiping it.
 */
class QrScanActivity : Activity(), SurfaceHolder.Callback, Camera.PreviewCallback {
    private lateinit var preview: SurfaceView
    private lateinit var hint: TextView
    private var camera: Camera? = null
    private val reader = MultiFormatReader()
    private var handedOff = false
    private var decoding = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        reader.setHints(mapOf(
            DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
            DecodeHintType.TRY_HARDER to true,
        ))
        preview = SurfaceView(this)
        hint = TextView(this).apply {
            text = "把镜头对准傻妞屏幕上的二维码"
            setTextColor(Color.WHITE)
            textSize = 16f
            setBackgroundColor(Color.argb(160, 0, 0, 0))
            setPadding(24, 24, 24, 24)
        }
        setContentView(FrameLayout(this).apply {
            setBackgroundColor(Color.BLACK)
            addView(preview, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            addView(hint, FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                gravity = android.view.Gravity.BOTTOM
            })
        })
        preview.holder.addCallback(this)
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION)
        }
    }

    override fun onRequestPermissionsResult(code: Int, names: Array<out String>, grants: IntArray) {
        super.onRequestPermissionsResult(code, names, grants)
        if (code == CAMERA_PERMISSION && (grants.isEmpty() ||
                grants.any { it != PackageManager.PERMISSION_GRANTED })) {
            hint.text = "未取得相机权限，无法扫码。"
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
        try {
            val opened = Camera.open()
            val parameters = opened.parameters
            parameters.previewSize?.let { parameters.setPreviewSize(it.width, it.height) }
            opened.parameters = parameters
            opened.setPreviewDisplay(holder)
            val size = opened.parameters.previewSize
            val buffer = ByteArray(size.width * size.height * 3 / 2)
            opened.addCallbackBuffer(buffer)
            opened.setPreviewCallbackWithBuffer(this)
            opened.startPreview()
            camera = opened
        } catch (error: Exception) {
            Log.w(TAG, "camera_unavailable")
            hint.text = "相机不可用，无法扫码。"
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

    override fun surfaceDestroyed(holder: SurfaceHolder) = releaseCamera()

    override fun onPreviewFrame(data: ByteArray, source: Camera) {
        val size = source.parameters.previewSize ?: return
        if (decoding || handedOff) {
            camera?.addCallbackBuffer(data)
            return
        }
        decoding = true
        try {
            val luminance = PlanarYUVLuminanceSource(
                data, size.width, size.height, 0, 0, size.width, size.height, false)
            val result = reader.decodeWithState(BinaryBitmap(HybridBinarizer(luminance)))
            val text = result.text
            if (text != null && text.startsWith(ClaimCode.PREFIX)) {
                handedOff = true
                setResult(RESULT_OK, Intent().putExtra(EXTRA_PAYLOAD, text))
                finish()
            }
        } catch (_: Exception) {
            /* A frame without a decodable code is the normal case. */
        } finally {
            reader.reset()
            decoding = false
            camera?.addCallbackBuffer(data)
        }
    }

    override fun onPause() {
        releaseCamera()
        super.onPause()
    }

    override fun onDestroy() {
        releaseCamera()
        super.onDestroy()
    }

    private fun releaseCamera() {
        val active = camera ?: return
        camera = null
        try {
            active.setPreviewCallbackWithBuffer(null)
            active.stopPreview()
            active.release()
        } catch (_: Exception) {
        }
    }

    companion object {
        const val EXTRA_PAYLOAD = "com.shaniu.companion.claim_code"
        private const val CAMERA_PERMISSION = 107
        private const val TAG = "ShaniuQrScan"
    }
}
