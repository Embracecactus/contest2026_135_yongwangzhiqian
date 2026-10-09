// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

internal object DeviceControlPresentation {
    data class Control(val enabled: Boolean, val reason: String)

    fun volume(state: DeviceControlSession.State): Control {
        val snapshot = state.snapshot
        return when {
            !state.authenticated -> Control(false, when (state.connection) {
                DeviceControlSession.Connection.CONNECTING -> "正在连接并验证设备"
                DeviceControlSession.Connection.RECONNECT_WAIT -> "连接中断，正在重连"
                else -> "连接并验证设备后设置"
            })
            state.writePending -> Control(false, "请求处理中，正在等待设备回读")
            !state.snapshotFresh -> Control(false, state.error ?: "正在读取设备音量")
            snapshot?.busy == true -> Control(false, "正在收音或播报，结束后可调整")
            snapshot?.volume == null -> Control(false, "设备音量暂不可用，正在重新读取")
            else -> Control(true, "${snapshot.volume}% · 可调节")
        }
    }
}
