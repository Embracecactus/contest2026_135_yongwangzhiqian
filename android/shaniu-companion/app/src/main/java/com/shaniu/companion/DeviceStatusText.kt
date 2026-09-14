// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import com.shaniu.companion.provision.DeviceControlProtocol

/** UI projection; a configured session or phone BLE link is not Wi-Fi evidence. */
internal fun DeviceControlProtocol.Snapshot.statusText(): String = when {
    memoryPending -> "正在处理记忆设置"
    busy -> "正在处理这次对话"
    !ready -> "手机已连接，语音服务尚未就绪"
    wifiReady == false -> "设备 Wi-Fi 未就绪，暂时无法发起云端对话"
    wifiReady == null -> "手机已连接，设备网络状态待确认"
    runtimeError == -125 -> "上次对话已取消，可以再次唤醒"
    runtimeError == -13 -> "请求被拒绝，请核对语音服务凭据或访问权限"
    runtimeError != null && runtimeError != 0 -> "上次对话未完成，请检查网络和语音服务配置"
    else -> "设备已连接 Wi-Fi"
}
