// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.os.Bundle
import android.view.WindowManager
import com.journeyapps.barcodescanner.CaptureActivity

class ClaimCaptureActivity : CaptureActivity() {
    override fun onCreate(state: Bundle?) {
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        super.onCreate(state)
    }
}
