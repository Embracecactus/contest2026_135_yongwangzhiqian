// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.net.URI
import java.nio.ByteBuffer

/** Generic secret CCF1 record; caller wipes key and output after authenticated delivery.
 * No client credential is included and no key is retained by this encoder.
 */
object CloudSettings {
    const val MIMO_TOKEN_PLAN_URL = "https://token-plan-cn.xiaomimimo.com/v1"
    const val MIMO_STANDARD_URL = "https://api.xiaomimimo.com/v1"
    enum class Dialect(val wire: Int) { OPENAI_CHAT_AUDIO(1), MIMO(2) }

    /** Validates cloud fields locally, without resolving or contacting the endpoint. */
    fun inputError(baseUrl: String, key: CharArray, asrModel: String,
                   chatModel: String, ttsModel: String): String? {
        val uri = try { URI(baseUrl) } catch (_: Exception) {
            return "语音服务 HTTPS 地址无效。"
        }
        if (uri.scheme != "https" || uri.rawUserInfo != null || uri.rawQuery != null || uri.rawFragment != null)
            return "语音服务地址必须是不含账号、查询或片段的 HTTPS 地址。"
        val host = uri.host ?: return "语音服务 HTTPS 地址缺少主机名。"
        if (host.length !in 1..127 || host.split('.').any {
                it.length !in 1..63 || !Regex("[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?").matches(it)
            }) return "语音服务主机名无效。"
        val port = if (uri.port == -1) 443 else uri.port
        if (port !in 1..65535) return "语音服务端口无效。"
        val path = uri.rawPath.ifEmpty { "/" }
        if (path.length !in 1..127 || !path.startsWith('/') ||
            !Regex("[a-zA-Z0-9/_.-]+").matches(path) || path.contains(".."))
            return "语音服务路径无效。"
        if (key.size !in 1..4096 || key.any { it.code !in 33..126 })
            return "语音服务 Key 应为 1 至 4096 个可打印 ASCII 字符。"
        val models = listOf("语音识别模型" to asrModel, "对话模型" to chatModel, "语音合成模型" to ttsModel)
        models.firstOrNull { (_, model) -> model.length !in 1..127 ||
            !Regex("[a-zA-Z0-9/_.:-]+").matches(model) }?.let { (name, _) ->
            return "$name 无效。"
        }
        return null
    }

    fun encode(baseUrl: String, key: CharArray, dialect: Dialect,
               asrModel: String, chatModel: String, ttsModel: String): ByteArray {
        inputError(baseUrl, key, asrModel, chatModel, ttsModel)?.let { require(false) { it } }
        val uri = URI(baseUrl)
        val host = checkNotNull(uri.host)
        val port = if (uri.port == -1) 443 else uri.port
        val path = uri.rawPath.ifEmpty { "/" }
        val models = listOf(asrModel, chatModel, ttsModel)
        val publicFields = listOf(host, path) + models
        val lengths = listOf(host.length, path.length, key.size) + models.map { it.length }
        val output = ByteBuffer.allocate(24 + lengths.sum())
        output.putInt(0x43434631).put(dialect.wire.toByte()).put(0).putShort(port.toShort())
        lengths.forEach { output.putShort(it.toShort()) }
        output.putInt(0)
        output.put(publicFields[0].toByteArray(Charsets.US_ASCII))
        output.put(publicFields[1].toByteArray(Charsets.US_ASCII))
        key.forEach { output.put(it.code.toByte()) }
        models.forEach { output.put(it.toByteArray(Charsets.US_ASCII)) }
        return output.array()
    }
}
