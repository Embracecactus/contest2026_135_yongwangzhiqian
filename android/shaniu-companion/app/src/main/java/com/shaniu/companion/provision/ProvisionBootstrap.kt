// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.CharArrayReader
import java.util.Base64

/** Owner-supplied QR data; not obtained from unauthenticated BLE advertisements.
 * No data-class generated toString/copy/component methods may expose the secret.
 * close() wipes owned decoded bytes, not scanner/Gson/JVM copies of input text.
 */
class ProvisionBootstrap private constructor(
    val deviceId: String,
    private val pin: ByteArray,
    private val secret: ByteArray,
    val screenBootstrap: Boolean = false,
) : AutoCloseable {
    private var closed = false

    /** Public trust anchor from owner-supplied activation data, not BLE advertising. */
    @Synchronized internal fun certificatePin(): String {
        check(!closed) { "Provisioning bootstrap is closed" }
        return pin.joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    @Synchronized fun newTls(): ProvisionTls {
        check(!closed) { "Provisioning bootstrap is closed" }
        return ProvisionTls(pin)
    }

    /** Called by the authenticated TLS owner only; does not itself authorize I/O.
     * A one-use temporary copy is cleared even when the consumer throws.
     */
    @Synchronized internal fun <T> usePossessionSecret(consume: (ByteArray) -> T): T {
        check(!closed) { "Provisioning bootstrap is closed" }
        val temporary = secret.copyOf()
        return try { consume(temporary) } finally { temporary.fill(0) }
    }

    @Synchronized override fun close() {
        secret.fill(0)
        pin.fill(0)
        closed = true
    }

    override fun toString(): String = "ProvisionBootstrap(redacted)"

    companion object {
        private val fields = setOf("protocol", "device_id", "certificate_sha256", "possession_secret")
        private val devicePattern = Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,127}")
        private val digestPattern = Regex("[0-9a-f]{64}")

        /** Shared boundary for the non-secret device locator returned to the
         * launcher after a committed provisioning transaction.
         */
        fun validDeviceId(value: String?): String? = value?.takeIf { devicePattern.matches(it) }

        /** SN1 is a canonical Base32 envelope from the physical display.
         * Its complete certificate pin authenticates TLS; the derived device
         * locator only finds a saved binding and is not a trust decision.
         */
        fun parseQr(input: String): ProvisionBootstrap {
            require(input.length == 107 && input.startsWith("SN1:")) { "Invalid device QR" }
            val alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
            val raw = ByteArray(64)
            var bits = 0
            var accumulator = 0
            var used = 0
            try {
                for (i in 4 until input.length) {
                    val n = alphabet.indexOf(input[i])
                    require(n >= 0)
                    accumulator = (accumulator shl 5) or n
                    bits += 5
                    if (bits >= 8) {
                        bits -= 8
                        require(used < raw.size)
                        raw[used++] = (accumulator ushr bits).toByte()
                        accumulator = accumulator and ((1 shl bits) - 1)
                    }
                }
                require(used == raw.size && accumulator == 0)
                require(raw.copyOfRange(0, 32).any { it != 0.toByte() })
                require((32 until 64).any { raw[it] != 0.toByte() })
                val locator = (0 until 8).joinToString("") { "%02x".format(raw[it].toInt() and 255) }
                return ProvisionBootstrap("shaniu-$locator", raw.copyOfRange(0, 32),
                    raw.copyOfRange(32, 64), screenBootstrap = true)
            } finally { raw.fill(0) }
        }

        /** Strict flat JSON, bounded before parsing. Caller must clear scanner/UI
         * buffers after return; parse failures deliberately omit input and cause.
         */
        fun parse(input: CharArray): ProvisionBootstrap {
            var pin: ByteArray? = null
            var secret: ByteArray? = null
            try {
                require(input.size in 1..1024)
                val values = mutableMapOf<String, String>()
                JsonReader(CharArrayReader(input)).use { reader ->
                    reader.strictness = Strictness.STRICT
                    reader.beginObject()
                    while (reader.hasNext()) {
                        val key = reader.nextName()
                        require(key in fields && key !in values)
                        require(reader.peek() == JsonToken.STRING)
                        values[key] = reader.nextString()
                    }
                    reader.endObject()
                    require(reader.peek() == JsonToken.END_DOCUMENT)
                }
                require(values.keys == fields && values["protocol"] == "provision-bootstrap-v1")
                val device = values.getValue("device_id")
                require(validDeviceId(device) != null)
                val digest = values.getValue("certificate_sha256")
                require(digestPattern.matches(digest))
                pin = ByteArray(32) { digest.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
                val encoded = values.getValue("possession_secret")
                require(encoded.length == 44)
                secret = Base64.getDecoder().decode(encoded)
                require(secret.size == 32 && Base64.getEncoder().encodeToString(secret) == encoded)
                return ProvisionBootstrap(device, pin, secret)
            } catch (_: Exception) {
                pin?.fill(0)
                secret?.fill(0)
                throw IllegalArgumentException("Invalid provisioning bootstrap")
            }
        }
    }
}
